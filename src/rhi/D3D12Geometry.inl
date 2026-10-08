Result<BufferHandle> Device::create_buffer(std::span<const std::byte> data, BufferUsage usage) noexcept {
    auto &p = *impl_;
    if (p.open || data.empty() || data.size() > UINT32_MAX ||
        data.size() % (usage == BufferUsage::vertex ? sizeof(Vertex) : 4))
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid buffer upload"});
    if (auto idle = wait_idle(); !idle)
        return std::unexpected(idle.error());
    auto h = p.buffers.emplace();
    if (!h)
        return h;
    auto &b = *p.buffers.get(*h);
    b.size = static_cast<std::uint32_t>(data.size());
    b.usage = usage;
    if (usage == BufferUsage::index)
        for (std::size_t offset = 0; offset < data.size(); offset += 4) {
            std::uint32_t index = 0;
            std::memcpy(&index, data.data() + offset, 4);
            b.max_index = std::max(b.max_index, index);
        }
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = data.size();
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    auto hr = p.device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&b.resource));
    ComPtr<ID3D12Resource> upload;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (SUCCEEDED(hr))
        hr = p.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&upload));
    void *mapped = nullptr;
    D3D12_RANGE empty{0, 0};
    if (SUCCEEDED(hr))
        hr = upload->Map(0, &empty, &mapped);
    if (SUCCEEDED(hr)) {
        std::memcpy(mapped, data.data(), data.size());
        upload->Unmap(0, nullptr);
    }
    auto &f = p.frames[0];
    if (SUCCEEDED(hr))
        hr = f.allocator->Reset();
    if (SUCCEEDED(hr))
        hr = f.command->Reset(f.allocator.Get(), nullptr);
    if (SUCCEEDED(hr)) {
        f.command->CopyBufferRegion(b.resource.Get(), 0, upload.Get(), 0, data.size());
        D3D12_BUFFER_BARRIER barrier{};
        barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
        barrier.SyncAfter =
            usage == BufferUsage::vertex ? D3D12_BARRIER_SYNC_VERTEX_SHADING : D3D12_BARRIER_SYNC_INDEX_INPUT;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
        barrier.AccessAfter = usage == BufferUsage::vertex ? D3D12_BARRIER_ACCESS_VERTEX_BUFFER
                                                           : D3D12_BARRIER_ACCESS_INDEX_BUFFER;
        barrier.pResource = b.resource.Get();
        barrier.Size = UINT64_MAX;
        D3D12_BARRIER_GROUP group{};
        group.Type = D3D12_BARRIER_TYPE_BUFFER;
        group.NumBarriers = 1;
        group.pBufferBarriers = &barrier;
        f.command->Barrier(1, &group);
        hr = f.command->Close();
    }
    if (FAILED(hr)) {
        const auto ignored = p.buffers.erase(*h);
        (void)ignored;
        return std::unexpected(gpu_error("Geometry upload failed", hr));
    }
    ID3D12CommandList *commands[]{f.command.Get()};
    p.queue->ExecuteCommandLists(1, commands);
    if (auto idle = wait_idle(); !idle)
        return std::unexpected(idle.error());
    return h;
}
Result<void> Device::destroy_buffer(BufferHandle h) noexcept {
    if (!impl_->buffers.get(h))
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid buffer"});
    if (auto idle = wait_idle(); !idle)
        return idle;
    return impl_->buffers.erase(h);
}
Result<PipelineHandle> Device::create_pipeline(PipelineDesc desc) noexcept {
    auto &p = *impl_;
    if (p.open || desc.vertex_shader.empty() || desc.pixel_shader.empty())
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid shader pipeline"});
    HRESULT hr = S_OK;
    if (!p.root) {
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants = {1, 0, sizeof(DrawConstants) / 4};
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC root{};
        root.NumParameters = 2;
        root.pParameters = params;
        root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob, error;
        hr = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
        if (SUCCEEDED(hr))
            hr = p.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                               IID_PPV_ARGS(&p.root));
        for (auto &f : p.frames) {
            if (FAILED(hr))
                break;
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC d{};
            d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            d.Width = 256;
            d.Height = 1;
            d.DepthOrArraySize = 1;
            d.MipLevels = 1;
            d.SampleDesc.Count = 1;
            d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            hr = p.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                   IID_PPV_ARGS(&f.constants));
            D3D12_RANGE empty{0, 0};
            if (SUCCEEDED(hr))
                hr = f.constants->Map(0, &empty, &f.mapped);
        }
    }
    if (FAILED(hr))
        return std::unexpected(gpu_error("Geometry root/constants failed", hr));
    auto h = p.pipelines.emplace();
    if (!h)
        return h;
    D3D12_INPUT_ELEMENT_DESC elements[]{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = p.root.Get();
    d.VS = {desc.vertex_shader.data(), desc.vertex_shader.size()};
    d.PS = {desc.pixel_shader.data(), desc.pixel_shader.size()};
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.DepthStencilState.DepthEnable = TRUE;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    if (!desc.grid)
        d.InputLayout = {elements, 2};
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    d.SampleDesc.Count = 1;
    auto &pipeline = *p.pipelines.get(*h);
    pipeline.grid = desc.grid;
    hr = p.device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pipeline.state));
    if (FAILED(hr)) {
        const auto ignored = p.pipelines.erase(*h);
        (void)ignored;
        return std::unexpected(gpu_error("Geometry PSO creation failed", hr));
    }
    return h;
}
Result<void> Device::destroy_pipeline(PipelineHandle h) noexcept {
    if (!impl_->pipelines.get(h))
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid pipeline"});
    if (auto idle = wait_idle(); !idle)
        return idle;
    return impl_->pipelines.erase(h);
}
Result<void> Device::begin_geometry(CommandList list, TextureHandle target,
                                    const FrameConstants &frame) noexcept {
    auto &p = *impl_;
    auto *t = p.textures.get(target);
    if (!t)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid geometry target"});
    if (p.geometry_target || !p.root)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Geometry scope unavailable"});
    if (auto ready = barrier(list, {target, t->state == Access::shader_read ? Stage::fragment : Stage::none,
                                    Stage::color_output, t->state, Access::render_target});
        !ready)
        return ready;
    auto &f = p.frames[p.slot];
    std::memcpy(f.mapped, &frame, sizeof(frame));
    auto depth = t->dsv->GetCPUDescriptorHandleForHeapStart();
    // Order previous depth writes even when the layout remains DEPTH_STENCIL_WRITE.
    D3D12_TEXTURE_BARRIER b{};
    b.SyncBefore = t->depth_ready ? D3D12_BARRIER_SYNC_DEPTH_STENCIL : D3D12_BARRIER_SYNC_NONE;
    b.SyncAfter = D3D12_BARRIER_SYNC_DEPTH_STENCIL;
    b.AccessBefore =
        t->depth_ready ? D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE : D3D12_BARRIER_ACCESS_NO_ACCESS;
    b.AccessAfter = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
    b.LayoutBefore =
        t->depth_ready ? D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE : D3D12_BARRIER_LAYOUT_UNDEFINED;
    b.LayoutAfter = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
    b.Flags = t->depth_ready ? D3D12_TEXTURE_BARRIER_FLAG_NONE : D3D12_TEXTURE_BARRIER_FLAG_DISCARD;
    b.pResource = t->depth.Get();
    b.Subresources = {0, 1, 0, 1, 0, 1};
    D3D12_BARRIER_GROUP group{};
    group.Type = D3D12_BARRIER_TYPE_TEXTURE;
    group.NumBarriers = 1;
    group.pTextureBarriers = &b;
    f.command->Barrier(1, &group);
    t->depth_ready = true;
    f.command->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    const float clear[]{0.08F, 0.12F, 0.2F, 1};
    f.command->ClearRenderTargetView(t->rtv, clear, 0, nullptr);
    f.command->OMSetRenderTargets(1, &t->rtv, FALSE, &depth);
    D3D12_VIEWPORT vp{0, 0, static_cast<float>(t->extent.width), static_cast<float>(t->extent.height), 0, 1};
    D3D12_RECT sc{0, 0, static_cast<LONG>(t->extent.width), static_cast<LONG>(t->extent.height)};
    f.command->RSSetViewports(1, &vp);
    f.command->RSSetScissorRects(1, &sc);
    f.command->SetGraphicsRootSignature(p.root.Get());
    f.command->SetGraphicsRootConstantBufferView(0, f.constants->GetGPUVirtualAddress());
    f.command->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p.geometry_target = target;
    return {};
}
Result<void> Device::draw_geometry(CommandList list, PipelineHandle pipeline, BufferHandle vertices,
                                   BufferHandle indices, std::uint32_t count,
                                   const DrawConstants &draw) noexcept {
    auto &p = *impl_;
    auto &f = p.frames[p.slot];
    if (!p.open || list.serial != p.serial || list.native != f.command.Get() || !p.geometry_target)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid geometry command"});
    auto *ps = p.pipelines.get(pipeline);
    if (!ps)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid pipeline"});
    f.command->SetPipelineState(ps->state.Get());
    f.command->SetGraphicsRoot32BitConstants(1, sizeof(draw) / 4, &draw, 0);
    if (ps->grid) {
        f.command->DrawInstanced(3, 1, 0, 0);
        return {};
    }
    auto *vb = p.buffers.get(vertices);
    auto *ib = p.buffers.get(indices);
    if (!vb || !ib)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid geometry buffer"});
    if (vb->usage != BufferUsage::vertex || ib->usage != BufferUsage::index || count > ib->size / 4 ||
        ib->max_index >= vb->size / sizeof(Vertex))
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid indexed draw"});
    D3D12_VERTEX_BUFFER_VIEW v{vb->resource->GetGPUVirtualAddress(), vb->size, sizeof(Vertex)};
    D3D12_INDEX_BUFFER_VIEW i{ib->resource->GetGPUVirtualAddress(), ib->size, DXGI_FORMAT_R32_UINT};
    f.command->IASetVertexBuffers(0, 1, &v);
    f.command->IASetIndexBuffer(&i);
    f.command->DrawIndexedInstanced(count, 1, 0, 0, 0);
    return {};
}
Result<void> Device::end_geometry(CommandList list, TextureHandle target) noexcept {
    auto &p = *impl_;
    if (p.geometry_target != target || !target)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Mismatched geometry scope"});
    auto result = barrier(
        list, {target, Stage::color_output, Stage::fragment, Access::render_target, Access::shader_read});
    if (result)
        p.geometry_target = {};
    return result;
}
Result<void> Device::capture_frame(const char *path) noexcept {
    auto &p = *impl_;
    if (p.open || !p.serial)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Capture requires a submitted frame"});
    if (auto idle = wait_idle(); !idle)
        return idle;
    auto &f = p.frames[p.slot];
    auto desc = p.backbuffers[p.slot]->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 size = 0;
    p.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &size);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = size;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    auto hr =
        p.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));
    if (SUCCEEDED(hr))
        hr = f.allocator->Reset();
    if (SUCCEEDED(hr))
        hr = f.command->Reset(f.allocator.Get(), nullptr);
    if (FAILED(hr))
        return std::unexpected(gpu_error("Capture storage failed", hr));
    D3D12_TEXTURE_BARRIER b{};
    b.SyncBefore = D3D12_BARRIER_SYNC_NONE;
    b.SyncAfter = D3D12_BARRIER_SYNC_COPY;
    b.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    b.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
    b.LayoutBefore = D3D12_BARRIER_LAYOUT_PRESENT;
    b.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
    b.pResource = p.backbuffers[p.slot].Get();
    b.Subresources = {0, 1, 0, 1, 0, 1};
    D3D12_BARRIER_GROUP group{};
    group.Type = D3D12_BARRIER_TYPE_TEXTURE;
    group.NumBarriers = 1;
    group.pTextureBarriers = &b;
    f.command->Barrier(1, &group);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = p.backbuffers[p.slot].Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = readback.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint = footprint;
    f.command->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    b.SyncBefore = D3D12_BARRIER_SYNC_COPY;
    b.SyncAfter = D3D12_BARRIER_SYNC_NONE;
    b.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
    b.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;
    b.LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
    b.LayoutAfter = D3D12_BARRIER_LAYOUT_PRESENT;
    f.command->Barrier(1, &group);
    hr = f.command->Close();
    if (FAILED(hr))
        return std::unexpected(gpu_error("Capture command failed", hr));
    ID3D12CommandList *commands[]{f.command.Get()};
    p.queue->ExecuteCommandLists(1, commands);
    if (auto idle = wait_idle(); !idle)
        return idle;
    void *mapped = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(size)};
    hr = readback->Map(0, &range, &mapped);
    if (FAILED(hr))
        return std::unexpected(gpu_error("Capture map failed", hr));
    auto result = detail::write_bmp(path, static_cast<const std::byte *>(mapped) + footprint.Offset,
                                    p.extent.width, p.extent.height, footprint.Footprint.RowPitch);
    D3D12_RANGE empty{0, 0};
    readback->Unmap(0, &empty);
    return result;
}
Result<void> Device::composite_texture(CommandList list, TextureHandle source) noexcept {
    auto &p = *impl_;
    auto &f = p.frames[p.slot];
    auto *t = p.textures.get(source);
    if (!t)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid composite source"});
    if (!p.open || list.serial != p.serial || list.native != f.command.Get() || p.geometry_target ||
        t->state != Access::shader_read || t->extent != p.extent)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid composite scope or extent"});
    D3D12_TEXTURE_BARRIER b[2]{};
    for (auto &item : b)
        item.Subresources = {0, 1, 0, 1, 0, 1};
    b[0].pResource = t->resource.Get();
    b[0].SyncBefore = D3D12_BARRIER_SYNC_PIXEL_SHADING;
    b[0].SyncAfter = D3D12_BARRIER_SYNC_COPY;
    b[0].AccessBefore = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    b[0].AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
    b[0].LayoutBefore = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    b[0].LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
    b[1].pResource = p.backbuffers[p.slot].Get();
    b[1].SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
    b[1].SyncAfter = D3D12_BARRIER_SYNC_COPY;
    b[1].AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
    b[1].AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
    b[1].LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    b[1].LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_DEST;
    D3D12_BARRIER_GROUP group{};
    group.Type = D3D12_BARRIER_TYPE_TEXTURE;
    group.NumBarriers = 2;
    group.pTextureBarriers = b;
    f.command->Barrier(1, &group);
    f.command->CopyResource(p.backbuffers[p.slot].Get(), t->resource.Get());
    for (auto &item : b) {
        std::swap(item.SyncBefore, item.SyncAfter);
        std::swap(item.AccessBefore, item.AccessAfter);
        std::swap(item.LayoutBefore, item.LayoutAfter);
    }
    f.command->Barrier(1, &group);
    return {};
}
