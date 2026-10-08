Result<BufferHandle> Device::create_buffer(std::span<const std::byte> data, BufferUsage usage) noexcept {
    auto &p = *impl_;
    if (p.open || data.empty() || data.size() > UINT32_MAX ||
        data.size() % (usage == BufferUsage::vertex ? sizeof(Vertex) : 4))
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid buffer upload"});
    if (auto idle = wait_idle(); !idle)
        return std::unexpected(idle.error());
    auto h = p.buffers.emplace(p.device);
    if (!h)
        return h;
    p.owned_buffers[h->index] = *h;
    auto &b = *p.buffers.get(*h);
    b.usage = usage;
    if (usage == BufferUsage::index)
        for (std::size_t offset = 0; offset < data.size(); offset += 4) {
            std::uint32_t index = 0;
            std::memcpy(&index, data.data() + offset, 4);
            b.max_index = std::max(b.max_index, index);
        }
    const auto fail = [&](Error error) -> Result<BufferHandle> {
        const auto ignored = p.buffers.erase(*h);
        (void)ignored;
        p.owned_buffers[h->index] = {};
        return std::unexpected(error);
    };
    auto ready = p.allocate_buffer(b, data.size(),
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       (usage == BufferUsage::vertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                                                     : VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!ready)
        return fail(ready.error());
    Impl::Buffer staging{p.device};
    ready = p.allocate_buffer(staging, data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!ready)
        return fail(ready.error());
    std::memcpy(staging.mapped, data.data(), data.size());
    auto &f = p.frames[0];
    auto status = vkResetCommandPool(p.device, f.pool, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (status == VK_SUCCESS)
        status = vkBeginCommandBuffer(f.command, &begin);
    if (status != VK_SUCCESS)
        return fail(failure("Upload command begin failed", status));
    VkBufferCopy copy{0, 0, data.size()};
    vkCmdCopyBuffer(f.command, staging.buffer, b.buffer, 1, &copy);
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    barrier.dstAccessMask =
        usage == BufferUsage::vertex ? VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT : VK_ACCESS_2_INDEX_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = b.buffer;
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(f.command, &dependency);
    status = vkEndCommandBuffer(f.command);
    VkCommandBufferSubmitInfo command{};
    command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command.commandBuffer = f.command;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    if (status == VK_SUCCESS)
        status = vkQueueSubmit2(p.queue, 1, &submit, VK_NULL_HANDLE);
    if (status == VK_SUCCESS)
        status = vkQueueWaitIdle(p.queue);
    if (status != VK_SUCCESS)
        return fail(failure("Geometry upload failed", status));
    return h;
}
Result<void> Device::destroy_buffer(BufferHandle h) noexcept {
    auto &p = *impl_;
    if (!p.buffers.get(h))
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid buffer"});
    if (auto idle = wait_idle(); !idle)
        return idle;
    p.owned_buffers[h.index] = {};
    return p.buffers.erase(h);
}
Result<PipelineHandle> Device::create_pipeline(PipelineDesc desc) noexcept {
    auto &p = *impl_;
    if (p.open || desc.vertex_shader.empty() || desc.pixel_shader.empty() || desc.vertex_shader.size() % 4 ||
        desc.pixel_shader.size() % 4)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid shader pipeline"});
    if (!p.pipeline_layout) {
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                             nullptr};
        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        auto status = vkCreateDescriptorSetLayout(p.device, &layout_info, nullptr, &p.constant_layout);
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                 sizeof(DrawConstants)};
        VkPipelineLayoutCreateInfo root{};
        root.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        root.setLayoutCount = 1;
        root.pSetLayouts = &p.constant_layout;
        root.pushConstantRangeCount = 1;
        root.pPushConstantRanges = &push;
        if (status == VK_SUCCESS)
            status = vkCreatePipelineLayout(p.device, &root, nullptr, &p.pipeline_layout);
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frames_in_flight};
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.maxSets = frames_in_flight;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        if (status == VK_SUCCESS)
            status = vkCreateDescriptorPool(p.device, &pool, nullptr, &p.constant_pool);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Geometry descriptor setup failed", status));
        for (std::size_t i = 0; i < frames_in_flight; ++i) {
            auto h = p.buffers.emplace(p.device);
            if (!h)
                return std::unexpected(h.error());
            p.owned_buffers[h->index] = *h;
            p.constants[i] = *h;
            auto &b = *p.buffers.get(*h);
            auto ready =
                p.allocate_buffer(b, sizeof(FrameConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (!ready)
                return std::unexpected(ready.error());
            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool = p.constant_pool;
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = &p.constant_layout;
            status = vkAllocateDescriptorSets(p.device, &alloc, &p.constant_sets[i]);
            if (status != VK_SUCCESS)
                return std::unexpected(failure("Constant descriptor allocation failed", status));
            VkDescriptorBufferInfo info{b.buffer, 0, sizeof(FrameConstants)};
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = p.constant_sets[i];
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &info;
            vkUpdateDescriptorSets(p.device, 1, &write, 0, nullptr);
        }
    }
    VkShaderModule modules[2]{};
    const std::span<const std::byte> binaries[]{desc.vertex_shader, desc.pixel_shader};
    for (int i = 0; i < 2; ++i) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = binaries[i].size();
        info.pCode = reinterpret_cast<const std::uint32_t *>(binaries[i].data());
        auto status = vkCreateShaderModule(p.device, &info, nullptr, &modules[i]);
        if (status != VK_SUCCESS) {
            if (modules[0])
                vkDestroyShaderModule(p.device, modules[0], nullptr);
            return std::unexpected(failure("Shader module failed", status));
        }
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = modules[i];
        stages[i].pName = desc.grid ? (i == 0 ? "GridVS" : "GridPS") : (i == 0 ? "VSMain" : "PSMain");
    }
    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[]{{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                              {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}};
    VkPipelineVertexInputStateCreateInfo vertex{};
    vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (!desc.grid) {
        vertex.vertexBindingDescriptionCount = 1;
        vertex.pVertexBindingDescriptions = &binding;
        vertex.vertexAttributeDescriptionCount = 2;
        vertex.pVertexAttributeDescriptions = attrs;
    }
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{};
    samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blends{};
    blends.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blends.attachmentCount = 1;
    blends.pAttachments = &blend;
    const VkDynamicState dynamics[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &vp;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &samples;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blends;
    info.pDynamicState = &dynamic;
    info.layout = p.pipeline_layout;
    auto h = p.pipelines.emplace(p.device);
    if (!h) {
        for (auto m : modules)
            vkDestroyShaderModule(p.device, m, nullptr);
        return h;
    }
    p.owned_pipelines[h->index] = *h;
    auto &pipeline = *p.pipelines.get(*h);
    pipeline.grid = desc.grid;
    auto status = vkCreateGraphicsPipelines(p.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline.pipeline);
    for (auto m : modules)
        vkDestroyShaderModule(p.device, m, nullptr);
    if (status != VK_SUCCESS) {
        const auto ignored = p.pipelines.erase(*h);
        (void)ignored;
        p.owned_pipelines[h->index] = {};
        return std::unexpected(failure("Geometry pipeline failed", status));
    }
    return h;
}
Result<void> Device::destroy_pipeline(PipelineHandle h) noexcept {
    auto &p = *impl_;
    if (!p.pipelines.get(h))
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid pipeline"});
    if (auto idle = wait_idle(); !idle)
        return idle;
    p.owned_pipelines[h.index] = {};
    return p.pipelines.erase(h);
}
Result<void> Device::begin_geometry(CommandList list, TextureHandle target,
                                    const FrameConstants &frame) noexcept {
    auto &p = *impl_;
    auto *t = p.textures.get(target);
    if (!t)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid geometry target"});
    if (p.geometry_target || !p.pipeline_layout)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Geometry scope unavailable"});
    if (auto ready = barrier(list, {target, t->state == Access::shader_read ? Stage::fragment : Stage::none,
                                    Stage::color_output, t->state, Access::render_target});
        !ready)
        return ready;
    // Depth storage is allocated on the cold target-creation path.
    auto command = p.frames[p.slot].command;
    auto *b = p.buffers.get(p.constants[p.slot]);
    std::memcpy(b->mapped, &frame, sizeof(frame));
    VkImageMemoryBarrier2 depth{};
    depth.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    depth.srcStageMask =
        t->depth_ready
            ? (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)
            : VK_PIPELINE_STAGE_2_NONE;
    depth.srcAccessMask = t->depth_ready ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0;
    depth.dstStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    depth.dstAccessMask =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depth.oldLayout = t->depth_ready ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    depth.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.srcQueueFamilyIndex = depth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depth.image = t->depth;
    depth.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &depth;
    vkCmdPipelineBarrier2(command, &dependency);
    t->depth_ready = true;
    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = t->view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.08F, 0.12F, 0.2F, 1}};
    VkRenderingAttachmentInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    ds.imageView = t->depth_view;
    ds.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    ds.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ds.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ds.clearValue.depthStencil = {1, 0};
    VkRenderingInfo render{};
    render.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    render.renderArea.extent = {t->extent.width, t->extent.height};
    render.layerCount = 1;
    render.colorAttachmentCount = 1;
    render.pColorAttachments = &color;
    render.pDepthAttachment = &ds;
    vkCmdBeginRendering(command, &render);
    VkViewport viewport{0, 0, static_cast<float>(t->extent.width), static_cast<float>(t->extent.height),
                        0, 1};
    VkRect2D scissor{{0, 0}, {t->extent.width, t->extent.height}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline_layout, 0, 1,
                            &p.constant_sets[p.slot], 0, nullptr);
    p.geometry_target = target;
    return {};
}
Result<void> Device::draw_geometry(CommandList list, PipelineHandle pipeline, BufferHandle vertices,
                                   BufferHandle indices, std::uint32_t count,
                                   const DrawConstants &draw) noexcept {
    auto &p = *impl_;
    auto command = p.frames[p.slot].command;
    if (!p.open || list.serial != p.serial || list.native != command || !p.geometry_target)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid geometry command"});
    auto *ps = p.pipelines.get(pipeline);
    if (!ps)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid pipeline"});
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ps->pipeline);
    vkCmdPushConstants(command, p.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(draw), &draw);
    if (ps->grid) {
        vkCmdDraw(command, 3, 1, 0, 0);
        return {};
    }
    auto *vb = p.buffers.get(vertices);
    auto *ib = p.buffers.get(indices);
    if (!vb || !ib)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid geometry buffer"});
    if (vb->usage != BufferUsage::vertex || ib->usage != BufferUsage::index || count > ib->size / 4 ||
        ib->max_index >= vb->size / sizeof(Vertex))
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid indexed draw"});
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vb->buffer, &offset);
    vkCmdBindIndexBuffer(command, ib->buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command, count, 1, 0, 0, 0);
    return {};
}
Result<void> Device::end_geometry(CommandList list, TextureHandle target) noexcept {
    auto &p = *impl_;
    if (p.geometry_target != target || !target || !p.open || list.serial != p.serial ||
        list.native != p.frames[p.slot].command)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Mismatched geometry scope"});
    vkCmdEndRendering(p.frames[p.slot].command);
    auto ready = barrier(
        list, {target, Stage::color_output, Stage::fragment, Access::render_target, Access::shader_read});
    if (ready) {
        p.last_geometry_target = target;
        p.geometry_target = {};
    }
    return ready;
}
Result<void> Device::capture_frame(const char *path) noexcept {
    auto &p = *impl_;
    auto *t = p.textures.get(p.last_geometry_target);
    if (p.open || !p.serial || !t || t->state != Access::shader_read)
        return std::unexpected(Error{ErrorCode::unsupported, "No retired scene color available for capture"});
    if (auto idle = wait_idle(); !idle)
        return idle;
    Impl::Buffer readback{p.device};
    auto ready =
        p.allocate_buffer(readback, static_cast<VkDeviceSize>(t->extent.width) * t->extent.height * 4,
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!ready)
        return ready;
    auto &f = p.frames[p.slot];
    auto status = vkResetCommandPool(p.device, f.pool, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (status == VK_SUCCESS)
        status = vkBeginCommandBuffer(f.command, &begin);
    if (status != VK_SUCCESS)
        return std::unexpected(failure("Capture command failed", status));
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    b.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t->image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(f.command, &dependency);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {t->extent.width, t->extent.height, 1};
    vkCmdCopyImageToBuffer(f.command, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                           &copy);
    b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    b.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier2(f.command, &dependency);
    VkBufferMemoryBarrier2 host{};
    host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = readback.buffer;
    host.size = VK_WHOLE_SIZE;
    dependency.imageMemoryBarrierCount = 0;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &host;
    vkCmdPipelineBarrier2(f.command, &dependency);
    status = vkEndCommandBuffer(f.command);
    VkCommandBufferSubmitInfo command{};
    command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command.commandBuffer = f.command;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    if (status == VK_SUCCESS)
        status = vkQueueSubmit2(p.queue, 1, &submit, VK_NULL_HANDLE);
    if (status == VK_SUCCESS)
        status = vkQueueWaitIdle(p.queue);
    if (status != VK_SUCCESS)
        return std::unexpected(failure("Capture submit failed", status));
    return detail::write_bmp(path, static_cast<const std::byte *>(readback.mapped), t->extent.width,
                             t->extent.height, static_cast<std::size_t>(t->extent.width) * 4, false);
}
Result<void> Device::composite_texture(CommandList list, TextureHandle source) noexcept {
    auto &p = *impl_;
    auto command = p.frames[p.slot].command;
    auto *t = p.textures.get(source);
    if (!t)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid composite source"});
    if (!p.blit_supported)
        return std::unexpected(Error{ErrorCode::unsupported, "Surface blit unavailable"});
    if (!p.open || list.serial != p.serial || list.native != command || p.geometry_target ||
        t->state != Access::shader_read || t->extent != p.extent)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid composite scope or extent"});
    VkImageMemoryBarrier2 b[2]{};
    for (auto &item : b) {
        item.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        item.srcQueueFamilyIndex = item.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        item.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    b[0].image = t->image;
    b[0].srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    b[0].dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    b[0].srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    b[0].dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    b[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[1].image = p.images[p.image_index];
    b[1].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    b[1].dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    b[1].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    b[1].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b[1].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 2;
    dependency.pImageMemoryBarriers = b;
    vkCmdPipelineBarrier2(command, &dependency);
    VkImageBlit2 region{};
    region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    region.srcSubresource = region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = region.dstOffsets[1] = {static_cast<std::int32_t>(p.extent.width),
                                                   static_cast<std::int32_t>(p.extent.height), 1};
    VkBlitImageInfo2 blit{};
    blit.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blit.srcImage = t->image;
    blit.dstImage = p.images[p.image_index];
    blit.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blit.regionCount = 1;
    blit.pRegions = &region;
    blit.filter = VK_FILTER_NEAREST;
    vkCmdBlitImage2(command, &blit);
    for (auto &item : b) {
        std::swap(item.srcStageMask, item.dstStageMask);
        std::swap(item.srcAccessMask, item.dstAccessMask);
        std::swap(item.oldLayout, item.newLayout);
    }
    vkCmdPipelineBarrier2(command, &dependency);
    return {};
}
