#include "Capture.hpp"
#include "rhi/NativeAccess.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include <vector>

namespace souls::rhi {
namespace {
constexpr std::uint32_t max_images = 8;
Error failure(const char *what, VkResult result) noexcept {
    return {ErrorCode::gpu, what, result};
}
VkImageLayout layout(Access access) noexcept {
    switch (access) {
    case Access::render_target:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case Access::shader_read:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case Access::present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}
VkPipelineStageFlags2 stage_mask(Stage stage) noexcept {
    switch (stage) {
    case Stage::color_output:
        return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    case Stage::fragment:
        return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    case Stage::all:
        return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    default:
        return VK_PIPELINE_STAGE_2_NONE;
    }
}
VkAccessFlags2 access_mask(Access access) noexcept {
    switch (access) {
    case Access::render_target:
        return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
    case Access::shader_read:
        return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    default:
        return VK_ACCESS_2_NONE;
    }
}
void transition(VkCommandBuffer command, VkImage image, Stage before, Stage after, Access from,
                Access to) noexcept {
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = stage_mask(before);
    b.dstStageMask = stage_mask(after);
    b.srcAccessMask = access_mask(from);
    b.dstAccessMask = access_mask(to);
    b.oldLayout = layout(from);
    b.newLayout = layout(to);
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(command, &dependency);
}
void render_scope(VkCommandBuffer command, VkImageView view, Extent extent, VkAttachmentLoadOp load,
                  Color clear) noexcept {
    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = load;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{clear.r, clear.g, clear.b, clear.a}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea.extent = {extent.width, extent.height};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    vkCmdBeginRendering(command, &info);
}
VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
    SDL_Log("Vulkan: %s", data->pMessage);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        static_cast<std::atomic_bool *>(user)->store(true, std::memory_order_relaxed);
    return VK_FALSE;
}
} // namespace
struct Device::Impl {
    struct Texture {
        VkDevice device = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkImage depth = VK_NULL_HANDLE;
        VkDeviceMemory depth_memory = VK_NULL_HANDLE;
        VkImageView depth_view = VK_NULL_HANDLE;
        bool depth_ready = false;
        Extent extent{};
        Access state = Access::undefined;
        explicit Texture(VkDevice owner) noexcept : device(owner) {}
        ~Texture() {
            if (depth_view)
                vkDestroyImageView(device, depth_view, nullptr);
            if (depth)
                vkDestroyImage(device, depth, nullptr);
            if (depth_memory)
                vkFreeMemory(device, depth_memory, nullptr);
            if (view)
                vkDestroyImageView(device, view, nullptr);
            if (image)
                vkDestroyImage(device, image, nullptr);
            if (memory)
                vkFreeMemory(device, memory, nullptr);
        }
    };
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        std::uint64_t value = 0;
        std::array<std::byte, 512 * 1024> storage{};
        LinearArena arena{storage};
    };
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR chain = VK_NULL_HANDLE;
    VkSemaphore timeline = VK_NULL_HANDLE;
    VkQueryPool queries = VK_NULL_HANDLE;
    std::array<VkImage, max_images> images{};
    std::array<VkImageView, max_images> views{};
    // Present waits belong to swapchain images, not CPU frame slots: acquisition
    // proves the previous present wait has consumed this image's semaphore.
    std::array<VkSemaphore, max_images> presented{};
    std::array<Access, max_images> states{};
    std::array<Frame, frames_in_flight> frames;
    Pool<Texture, TextureTag, 32> textures;
    struct Buffer {
        VkDevice device = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void *mapped = nullptr;
        std::uint32_t size = 0, max_index = 0;
        BufferUsage usage = BufferUsage::vertex;
        explicit Buffer(VkDevice d) noexcept : device(d) {}
        ~Buffer() {
            if (mapped)
                vkUnmapMemory(device, memory);
            if (buffer)
                vkDestroyBuffer(device, buffer, nullptr);
            if (memory)
                vkFreeMemory(device, memory, nullptr);
        }
    };
    struct Pipeline {
        VkDevice device = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        bool grid = false;
        explicit Pipeline(VkDevice d) noexcept : device(d) {}
        ~Pipeline() {
            if (pipeline)
                vkDestroyPipeline(device, pipeline, nullptr);
        }
    };
    Pool<Buffer, BufferTag, 128> buffers;
    Pool<Pipeline, PipelineTag, 16> pipelines;
    std::array<BufferHandle, 128> owned_buffers{};
    std::array<PipelineHandle, 16> owned_pipelines{};
    std::array<BufferHandle, frames_in_flight> constants{};
    std::array<VkDescriptorSet, frames_in_flight> constant_sets{};
    VkDescriptorSetLayout constant_layout = VK_NULL_HANDLE;
    VkDescriptorPool constant_pool = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    TextureHandle geometry_target{}, last_geometry_target{};
    Result<void> allocate_buffer(Buffer &b, VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties_needed) noexcept {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        auto status = vkCreateBuffer(device, &info, nullptr, &b.buffer);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Buffer creation failed", status));
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device, b.buffer, &req);
        std::uint32_t type = UINT32_MAX;
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((req.memoryTypeBits & (1U << i)) &&
                (memory.memoryTypes[i].propertyFlags & properties_needed) == properties_needed) {
                type = i;
                break;
            }
        if (type == UINT32_MAX)
            return std::unexpected(Error{ErrorCode::unsupported, "Required buffer memory unavailable"});
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = type;
        status = vkAllocateMemory(device, &alloc, nullptr, &b.memory);
        if (status == VK_SUCCESS)
            status = vkBindBufferMemory(device, b.buffer, b.memory, 0);
        if (status == VK_SUCCESS && (properties_needed & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            status = vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Buffer memory failed", status));
        b.size = static_cast<std::uint32_t>(size);
        return {};
    }
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceProperties properties{};
    std::uint32_t family = 0, image_count = 0, min_images = 2, slot = 0, image_index = 0, timestamp_bits = 0;
    std::uint64_t value = 0, serial = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorspace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    Extent extent{};
    Stats telemetry{};
    bool blit_supported = false;
    bool open = false, vsync = true, memory_budget = false, recreate = false;
    std::atomic_bool validation_error{false};
    void destroy_chain() noexcept {
        for (std::uint32_t i = 0; i < image_count; ++i) {
            if (views[i])
                vkDestroyImageView(device, views[i], nullptr);
            if (presented[i])
                vkDestroySemaphore(device, presented[i], nullptr);
            views[i] = VK_NULL_HANDLE;
            presented[i] = VK_NULL_HANDLE;
        }
        if (chain)
            vkDestroySwapchainKHR(device, chain, nullptr);
        chain = VK_NULL_HANDLE;
        image_count = 0;
    }
    ~Impl() {
        // Texture members destruct after this body; they must be released while
        // VkDevice still exists. Enumerate live generation IDs via stored handles.
        for (auto h : owned)
            if (h) {
                const auto ignored = textures.erase(h);
                (void)ignored;
            }
        for (auto h : owned_pipelines)
            if (h) {
                const auto ignored = pipelines.erase(h);
                (void)ignored;
            }
        for (auto h : owned_buffers)
            if (h) {
                const auto ignored = buffers.erase(h);
                (void)ignored;
            }
        if (device) {
            if (pipeline_layout)
                vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            if (constant_pool)
                vkDestroyDescriptorPool(device, constant_pool, nullptr);
            if (constant_layout)
                vkDestroyDescriptorSetLayout(device, constant_layout, nullptr);
            destroy_chain();
            for (auto &frame : frames) {
                if (frame.pool)
                    vkDestroyCommandPool(device, frame.pool, nullptr);
                if (frame.acquired)
                    vkDestroySemaphore(device, frame.acquired, nullptr);
            }
            if (queries)
                vkDestroyQueryPool(device, queries, nullptr);
            if (timeline)
                vkDestroySemaphore(device, timeline, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        if (debug)
            vkDestroyDebugUtilsMessengerEXT(instance, debug, nullptr);
        if (instance)
            vkDestroyInstance(instance, nullptr);
    }
    std::array<TextureHandle, 32> owned{};
    Result<void> wait_value(std::uint64_t next) noexcept {
        if (!next)
            return {};
        VkSemaphoreWaitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        info.semaphoreCount = 1;
        info.pSemaphores = &timeline;
        info.pValues = &next;
        auto status = vkWaitSemaphores(device, &info, 10000000000ULL);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Timeline wait failed", status));
        return {};
    }
    Result<void> create_chain(Extent requested) noexcept {
        VkSurfaceCapabilitiesKHR caps{};
        auto status = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Surface capabilities failed", status));
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            return std::unexpected(
                Error{ErrorCode::unsupported, "Surface does not support color attachments"});
        if (caps.currentExtent.width != UINT32_MAX)
            extent = {caps.currentExtent.width, caps.currentExtent.height};
        else
            extent = {std::clamp(requested.width, caps.minImageExtent.width, caps.maxImageExtent.width),
                      std::clamp(requested.height, caps.minImageExtent.height, caps.maxImageExtent.height)};
        if (!extent.width || !extent.height)
            return std::unexpected(Error{ErrorCode::suspended, "Surface minimized"});

        blit_supported = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0;
        VkFormatProperties src_features{}, dst_features{};
        vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, &src_features);
        vkGetPhysicalDeviceFormatProperties(physical, format, &dst_features);
        blit_supported = blit_supported &&
                         (src_features.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
                         (dst_features.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
        min_images = std::max(2U, caps.minImageCount);
        std::uint32_t desired = min_images + 1;
        if (caps.maxImageCount)
            desired = std::min(desired, caps.maxImageCount);
        if (desired < min_images || desired > max_images)
            return std::unexpected(Error{ErrorCode::unsupported, "Unsupported surface image count"});
        std::uint32_t mode_count = 0;
        status = vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &mode_count, nullptr);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Present mode count failed", status));
        std::vector<VkPresentModeKHR> modes(mode_count);
        status = vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &mode_count, modes.data());
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Present modes failed", status));
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        if (!vsync && std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
            mode = VK_PRESENT_MODE_MAILBOX_KHR;
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (auto choice : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (caps.supportedCompositeAlpha & choice) {
                alpha = choice;
                break;
            }
        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = desired;
        info.imageFormat = format;
        info.imageColorSpace = colorspace;
        info.imageExtent = {extent.width, extent.height};
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          (blit_supported ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0);
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = alpha;
        info.presentMode = mode;
        info.clipped = VK_TRUE;
        // Device is idle; destroy before recreation. Failures abort the shell.
        destroy_chain();
        status = vkCreateSwapchainKHR(device, &info, nullptr, &chain);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Swapchain creation failed", status));
        status = vkGetSwapchainImagesKHR(device, chain, &image_count, nullptr);
        if (status != VK_SUCCESS || image_count > max_images) {
            image_count = 0;
            return std::unexpected(failure("Swapchain image count exceeds fixed capacity", status));
        }
        status = vkGetSwapchainImagesKHR(device, chain, &image_count, images.data());
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Swapchain images failed", status));
        for (std::uint32_t i = 0; i < image_count; ++i) {
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = images[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            status = vkCreateImageView(device, &view, nullptr, &views[i]);
            if (status != VK_SUCCESS)
                return std::unexpected(failure("Swapchain view creation failed", status));
            VkSemaphoreCreateInfo sem{};
            sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            status = vkCreateSemaphore(device, &sem, nullptr, &presented[i]);
            if (status != VK_SUCCESS)
                return std::unexpected(failure("Present semaphore creation failed", status));
            states[i] = Access::undefined;
        }
        recreate = false;
        return {};
    }
};
Device::Device() noexcept = default;
Device::Device(Device &&) noexcept = default;
Device &Device::operator=(Device &&other) noexcept {
    if (this != &other) {
        if (impl_ && impl_->device) {
            const auto ignored = wait_idle();
            (void)ignored;
        }
        impl_ = std::move(other.impl_);
    }
    return *this;
}
Device::~Device() {
    if (impl_ && impl_->device) {
        const auto ignored = wait_idle();
        (void)ignored;
    }
}
Result<Device> Device::create(DeviceDesc desc) noexcept {
    if (!desc.window)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Device requires a window"});
    auto status = volkInitialize();
    if (status != VK_SUCCESS)
        return std::unexpected(failure("Vulkan loader unavailable", status));
    Device result;
    result.impl_.reset(new (std::nothrow) Impl);
    if (!result.impl_)
        return std::unexpected(Error{ErrorCode::exhausted, "Device allocation failed"});
    auto &p = *result.impl_;
    p.vsync = desc.vsync;
    std::uint32_t extension_count = 0;
    const char *const *required = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if (!required)
        return std::unexpected(Error{ErrorCode::platform, SDL_GetError()});
    std::vector<const char *> extensions(required, required + extension_count);
    const char *validation = "VK_LAYER_KHRONOS_validation";
    if (desc.validation) {
        std::uint32_t count = 0;
        status = vkEnumerateInstanceLayerProperties(&count, nullptr);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Layer enumeration failed", status));
        std::vector<VkLayerProperties> layers(count);
        status = vkEnumerateInstanceLayerProperties(&count, layers.data());
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Layer enumeration failed", status));
        if (std::none_of(layers.begin(), layers.end(), [validation](const auto &layer) {
                return std::strcmp(layer.layerName, validation) == 0;
            }))
            return std::unexpected(
                Error{ErrorCode::unsupported, "Install VK_LAYER_KHRONOS_validation or omit --validation"});
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Souls Engine";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instance{};
    instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance.pApplicationInfo = &app;
    instance.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    instance.ppEnabledExtensionNames = extensions.data();
    VkDebugUtilsMessengerCreateInfoEXT debug{};
    debug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = debug_message;
    debug.pUserData = &p.validation_error;
    if (desc.validation) {
        instance.enabledLayerCount = 1;
        instance.ppEnabledLayerNames = &validation;
        instance.pNext = &debug;
    }
    status = vkCreateInstance(&instance, nullptr, &p.instance);
    if (status != VK_SUCCESS)
        return std::unexpected(failure("Vulkan 1.3 instance creation failed", status));
    volkLoadInstance(p.instance);
    if (desc.validation) {
        status = vkCreateDebugUtilsMessengerEXT(p.instance, &debug, nullptr, &p.debug);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Validation messenger creation failed", status));
    }
    if (!SDL_Vulkan_CreateSurface(desc.window, p.instance, nullptr, &p.surface))
        return std::unexpected(Error{ErrorCode::platform, SDL_GetError()});
    std::uint32_t device_count = 0;
    status = vkEnumeratePhysicalDevices(p.instance, &device_count, nullptr);
    if (status != VK_SUCCESS)
        return std::unexpected(failure("GPU enumeration failed", status));
    std::vector<VkPhysicalDevice> devices(device_count);
    status = vkEnumeratePhysicalDevices(p.instance, &device_count, devices.data());
    if (status != VK_SUCCESS)
        return std::unexpected(failure("GPU enumeration failed", status));
    int best_score = -1;
    for (auto physical : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physical, &props);
        if (props.apiVersion < VK_API_VERSION_1_3)
            continue;
        VkPhysicalDeviceVulkan13Features f13{};
        f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceVulkan12Features f12{};
        f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        f12.pNext = &f13;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &f12;
        vkGetPhysicalDeviceFeatures2(physical, &features);
        if (!f12.timelineSemaphore || !f13.dynamicRendering || !f13.synchronization2)
            continue;
        std::uint32_t count = 0;
        status = vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
        if (status != VK_SUCCESS)
            continue;
        std::vector<VkExtensionProperties> exts(count);
        status = vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, exts.data());
        if (status != VK_SUCCESS)
            continue;
        const auto has = [&exts](const char *name) {
            return std::any_of(exts.begin(), exts.end(),
                               [name](const auto &e) { return std::strcmp(e.extensionName, name) == 0; });
        };
        if (!has(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;
        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, queues.data());
        for (std::uint32_t i = 0; i < families; ++i) {
            VkBool32 present = VK_FALSE;
            status = vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, p.surface, &present);
            if (status != VK_SUCCESS || !present || !(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                continue;
            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score > best_score) {
                best_score = score;
                p.physical = physical;
                p.family = i;
                p.properties = props;
                p.timestamp_bits = queues[i].timestampValidBits;
                p.memory_budget = has(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
            }
            break;
        }
    }
    if (!p.physical)
        return std::unexpected(
            Error{ErrorCode::unsupported,
                  "No Vulkan 1.3 GPU with sync2, timeline, dynamic rendering and present queue"});
    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = p.family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.pNext = &f13;
    f12.timelineSemaphore = VK_TRUE;
    const char *device_extensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME};
    VkDeviceCreateInfo device{};
    device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device.pNext = &f12;
    device.queueCreateInfoCount = 1;
    device.pQueueCreateInfos = &queue;
    device.enabledExtensionCount = p.memory_budget ? 2U : 1U;
    device.ppEnabledExtensionNames = device_extensions;
    status = vkCreateDevice(p.physical, &device, nullptr, &p.device);
    if (status != VK_SUCCESS)
        return std::unexpected(failure("Vulkan device creation failed", status));
    volkLoadDevice(p.device);
    vkGetDeviceQueue(p.device, p.family, 0, &p.queue);
    vkGetPhysicalDeviceMemoryProperties(p.physical, &p.memory);
    std::uint32_t format_count = 0;
    status = vkGetPhysicalDeviceSurfaceFormatsKHR(p.physical, p.surface, &format_count, nullptr);
    if (status != VK_SUCCESS || !format_count)
        return std::unexpected(failure("Surface formats unavailable", status));
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    status = vkGetPhysicalDeviceSurfaceFormatsKHR(p.physical, p.surface, &format_count, formats.data());
    if (status != VK_SUCCESS)
        return std::unexpected(failure("Surface format query failed", status));
    p.format = formats[0].format;
    p.colorspace = formats[0].colorSpace;
    for (auto format : formats)
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            p.format = format.format;
            p.colorspace = format.colorSpace;
            break;
        }
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(desc.window, &width, &height))
        return std::unexpected(Error{ErrorCode::platform, SDL_GetError()});
    if (auto chain = p.create_chain({static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)});
        !chain)
        return std::unexpected(chain.error());
    VkSemaphoreTypeCreateInfo type{};
    type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo sem{};
    sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sem.pNext = &type;
    status = vkCreateSemaphore(p.device, &sem, nullptr, &p.timeline);
    if (status != VK_SUCCESS)
        return std::unexpected(failure("Timeline creation failed", status));
    sem.pNext = nullptr;
    for (auto &frame : p.frames) {
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.queueFamilyIndex = p.family;
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        status = vkCreateCommandPool(p.device, &pool, nullptr, &frame.pool);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Command pool creation failed", status));
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = frame.pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        status = vkAllocateCommandBuffers(p.device, &allocate, &frame.command);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Command allocation failed", status));
        status = vkCreateSemaphore(p.device, &sem, nullptr, &frame.acquired);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Acquire semaphore creation failed", status));
    }
    if (p.timestamp_bits) {
        VkQueryPoolCreateInfo query{};
        query.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query.queryCount = frames_in_flight * 2;
        status = vkCreateQueryPool(p.device, &query, nullptr, &p.queries);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Timestamp pool creation failed", status));
    }
    return result;
}
Result<void> Device::wait(TimelinePoint point) noexcept {
    if (point.value > impl_->value)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Timeline point has not been submitted"});
    return impl_->wait_value(point.value);
}
Result<void> Device::wait_idle() noexcept {
    if (impl_->open)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Cannot drain an open frame"});
    auto status = vkDeviceWaitIdle(impl_->device);
    if (status != VK_SUCCESS)
        return std::unexpected(failure("GPU drain failed", status));
    if (impl_->validation_error.load(std::memory_order_relaxed))
        return std::unexpected(Error{ErrorCode::gpu, "Vulkan validation error; see log"});
    return {};
}
Result<void> Device::resize(Extent pixels) noexcept {
    if (auto idle = wait_idle(); !idle)
        return idle;
    if (!pixels.width || !pixels.height) {
        impl_->extent = pixels;
        return {};
    }
    return impl_->create_chain(pixels);
}
Result<CommandList> Device::begin_frame(Color clear) noexcept {
    auto &p = *impl_;
    if (p.validation_error.load(std::memory_order_relaxed))
        return std::unexpected(Error{ErrorCode::gpu, "Vulkan validation error; see log"});
    if (p.open)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Frame already open"});
    if (!p.extent.width || !p.extent.height)
        return std::unexpected(Error{ErrorCode::suspended, "Window minimized"});
    if (p.recreate) {
        if (auto rebuilt = resize(p.extent); !rebuilt)
            return std::unexpected(rebuilt.error());
    }
    p.slot = static_cast<std::uint32_t>(p.serial % frames_in_flight);
    auto &f = p.frames[p.slot];
    if (auto ready = p.wait_value(f.value); !ready)
        return std::unexpected(ready.error());
    if (f.value && p.queries) {
        std::array<std::uint64_t, 2> times{};
        auto status = vkGetQueryPoolResults(p.device, p.queries, p.slot * 2, 2, sizeof(times), times.data(),
                                            sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
        if (status != VK_SUCCESS)
            return std::unexpected(failure("Timestamp readback failed", status));
        auto mask = p.timestamp_bits == 64 ? UINT64_MAX : ((std::uint64_t{1} << p.timestamp_bits) - 1);
        p.telemetry.gpu_us = static_cast<double>((times[1] - times[0]) & mask) *
                             static_cast<double>(p.properties.limits.timestampPeriod) / 1000.0;
        p.telemetry.gpu_timing_valid = true;
    }
    auto status =
        vkAcquireNextImageKHR(p.device, p.chain, 1000000000ULL, f.acquired, VK_NULL_HANDLE, &p.image_index);
    if (status == VK_ERROR_OUT_OF_DATE_KHR) {
        p.recreate = true;
        return std::unexpected(Error{ErrorCode::suspended, "Swapchain recreation required"});
    }
    if (status == VK_SUBOPTIMAL_KHR)
        p.recreate = true;
    else if (status != VK_SUCCESS)
        return std::unexpected(failure("Swapchain acquire failed", status));
    status = vkResetCommandPool(p.device, f.pool, 0);
    if (status != VK_SUCCESS)
        return std::unexpected(failure("Command pool reset failed", status));
    VkCommandBufferBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    status = vkBeginCommandBuffer(f.command, &info);
    if (status != VK_SUCCESS)
        return std::unexpected(failure("Command begin failed", status));
    f.arena.reset();
    p.open = true;
    ++p.serial;
    if (p.queries) {
        vkCmdResetQueryPool(f.command, p.queries, p.slot * 2, 2);
        vkCmdWriteTimestamp2(f.command, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, p.queries, p.slot * 2);
    }
    transition(f.command, p.images[p.image_index], Stage::none, Stage::color_output, p.states[p.image_index],
               Access::render_target);
    render_scope(f.command, p.views[p.image_index], p.extent, VK_ATTACHMENT_LOAD_OP_CLEAR, clear);
    vkCmdEndRendering(f.command);
    p.states[p.image_index] = Access::render_target;
    return CommandList{f.command, p.serial};
}
Result<TimelinePoint> Device::end_frame(CommandList list) noexcept {
    auto &p = *impl_;
    auto &f = p.frames[p.slot];
    if (!p.open || list.serial != p.serial || list.native != f.command)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Expired command token"});
    if (p.geometry_target)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Geometry scope still open"});
    transition(f.command, p.images[p.image_index], Stage::color_output, Stage::none, Access::render_target,
               Access::present);
    p.states[p.image_index] = Access::present;
    if (p.queries)
        vkCmdWriteTimestamp2(f.command, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, p.queries, p.slot * 2 + 1);
    auto status = vkEndCommandBuffer(f.command);
    p.open = false;
    if (status != VK_SUCCESS)
        return std::unexpected(failure("Command end failed", status));
    VkSemaphoreSubmitInfo acquired{};
    acquired.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    acquired.semaphore = f.acquired;
    acquired.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    std::array<VkSemaphoreSubmitInfo, 2> signals{};
    signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signals[0].semaphore = p.presented[p.image_index];
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signals[1].semaphore = p.timeline;
    signals[1].value = ++p.value;
    signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo command{};
    command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command.commandBuffer = f.command;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &acquired;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    submit.signalSemaphoreInfoCount = 2;
    submit.pSignalSemaphoreInfos = signals.data();
    status = vkQueueSubmit2(p.queue, 1, &submit, VK_NULL_HANDLE);
    if (status != VK_SUCCESS)
        return std::unexpected(failure("Queue submit2 failed", status));
    f.value = p.value;
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &p.presented[p.image_index];
    present.swapchainCount = 1;
    present.pSwapchains = &p.chain;
    present.pImageIndices = &p.image_index;
    status = vkQueuePresentKHR(p.queue, &present);
    if (status == VK_ERROR_OUT_OF_DATE_KHR || status == VK_SUBOPTIMAL_KHR)
        p.recreate = true;
    else if (status != VK_SUCCESS)
        return std::unexpected(failure("Present failed", status));
    return TimelinePoint{p.value};
}
Result<TextureHandle> Device::create_render_target(Extent extent) noexcept {
    auto &p = *impl_;
    if (p.open || !extent.width || !extent.height)
        return std::unexpected(
            Error{ErrorCode::invalid_argument,
                  "Render target creation requires a closed frame and positive dimensions"});
    auto handle = p.textures.emplace(p.device);
    if (!handle)
        return std::unexpected(handle.error());
    p.owned[handle->index] = *handle;
    auto &texture = *p.textures.get(*handle);
    texture.extent = extent;
    const auto fail = [&p, handle](const char *what, VkResult status) -> Result<TextureHandle> {
        const auto ignored = p.textures.erase(*handle);
        (void)ignored;
        p.owned[handle->index] = {};
        return std::unexpected(failure(what, status));
    };
    VkImageCreateInfo image{};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {extent.width, extent.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    auto status = vkCreateImage(p.device, &image, nullptr, &texture.image);
    if (status != VK_SUCCESS)
        return fail("Viewport image creation failed", status);
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(p.device, texture.image, &requirements);
    std::uint32_t type = UINT32_MAX;
    for (std::uint32_t i = 0; i < p.memory.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1U << i)) &&
            (p.memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            type = i;
            break;
        }
    if (type == UINT32_MAX)
        return fail("No device-local memory type", VK_ERROR_FEATURE_NOT_PRESENT);
    VkMemoryAllocateInfo memory{};
    memory.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory.allocationSize = requirements.size;
    memory.memoryTypeIndex = type;
    status = vkAllocateMemory(p.device, &memory, nullptr, &texture.memory);
    if (status != VK_SUCCESS)
        return fail("Viewport memory allocation failed", status);
    status = vkBindImageMemory(p.device, texture.image, texture.memory, 0);
    if (status != VK_SUCCESS)
        return fail("Viewport memory bind failed", status);
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = texture.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = image.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    status = vkCreateImageView(p.device, &view, nullptr, &texture.view);
    if (status != VK_SUCCESS)
        return fail("Viewport view creation failed", status);
    image.format = VK_FORMAT_D32_SFLOAT;
    image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    status = vkCreateImage(p.device, &image, nullptr, &texture.depth);
    if (status != VK_SUCCESS)
        return fail("Depth image failed", status);
    vkGetImageMemoryRequirements(p.device, texture.depth, &requirements);
    type = UINT32_MAX;
    for (std::uint32_t i = 0; i < p.memory.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1U << i)) &&
            (p.memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            type = i;
            break;
        }
    if (type == UINT32_MAX)
        return fail("Depth memory unavailable", VK_ERROR_FEATURE_NOT_PRESENT);
    memory.allocationSize = requirements.size;
    memory.memoryTypeIndex = type;
    status = vkAllocateMemory(p.device, &memory, nullptr, &texture.depth_memory);
    if (status == VK_SUCCESS)
        status = vkBindImageMemory(p.device, texture.depth, texture.depth_memory, 0);
    if (status != VK_SUCCESS)
        return fail("Depth memory failed", status);
    view.image = texture.depth;
    view.format = image.format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    status = vkCreateImageView(p.device, &view, nullptr, &texture.depth_view);
    if (status != VK_SUCCESS)
        return fail("Depth view failed", status);
    return *handle;
}
Result<void> Device::destroy_texture(TextureHandle texture) noexcept {
    if (!impl_->textures.get(texture))
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid texture"});
    if (auto idle = wait_idle(); !idle)
        return idle;
    impl_->owned[texture.index] = {};
    return impl_->textures.erase(texture);
}
Result<void> Device::barrier(CommandList list, TextureBarrier b) noexcept {
    auto &p = *impl_;
    auto *t = p.textures.get(b.texture);
    if (!p.open || list.serial != p.serial || list.native != p.frames[p.slot].command)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Expired command token"});
    if (!t)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid texture"});
    if (t->state != b.from || b.to == Access::undefined || b.to == Access::present)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid texture barrier state"});
    transition(p.frames[p.slot].command, t->image, b.before, b.after, b.from, b.to);
    t->state = b.to;
    return {};
}
Result<void> Device::clear_texture(CommandList list, TextureHandle texture, Color clear) noexcept {
    auto *t = impl_->textures.get(texture);
    if (!t)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid texture"});
    const auto from = t->state;
    if (auto ready = barrier(list, {texture, from == Access::shader_read ? Stage::fragment : Stage::none,
                                    Stage::color_output, from, Access::render_target});
        !ready)
        return ready;
    auto command = impl_->frames[impl_->slot].command;
    render_scope(command, t->view, t->extent, VK_ATTACHMENT_LOAD_OP_CLEAR, clear);
    vkCmdEndRendering(command);
    return barrier(
        list, {texture, Stage::color_output, Stage::fragment, Access::render_target, Access::shader_read});
}
Swapchain Device::swapchain() const noexcept {
    return {impl_->extent, impl_->image_count};
}
Stats Device::stats() const noexcept {
    auto stats = impl_->telemetry;
    if (impl_->memory_budget) {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
        budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
        VkPhysicalDeviceMemoryProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        properties.pNext = &budget;
        vkGetPhysicalDeviceMemoryProperties2(impl_->physical, &properties);
        for (std::uint32_t i = 0; i < properties.memoryProperties.memoryHeapCount; ++i)
            if (properties.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                stats.local_usage += budget.heapUsage[i];
                stats.local_budget += budget.heapBudget[i];
            }
        stats.budget_valid = true;
    }
    return stats;
}
LinearArena &Device::frame_arena() noexcept {
    assert(impl_->open);
    return impl_->frames[impl_->slot].arena;
}
const char *Device::adapter_name() const noexcept {
    return impl_->properties.deviceName;
}
detail::NativeContext detail::NativeAccess::context(Device &device) noexcept {
    const auto &p = *device.impl_;
    return {p.instance, p.physical, p.device, p.queue, p.family, p.format, p.min_images, p.image_count};
}
Result<detail::TextureView> detail::NativeAccess::texture(Device &device, TextureHandle handle) noexcept {
    auto *t = device.impl_->textures.get(handle);
    if (!t)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid texture"});
    return detail::TextureView{t->view};
}
void detail::NativeAccess::begin_overlay(Device &device, CommandList list) noexcept {
    const auto &p = *device.impl_;
    assert(p.open && list.serial == p.serial);
    render_scope(static_cast<VkCommandBuffer>(list.native), p.views[p.image_index], p.extent,
                 VK_ATTACHMENT_LOAD_OP_LOAD, {});
}
void detail::NativeAccess::end_overlay(Device &, CommandList list) noexcept {
    vkCmdEndRendering(static_cast<VkCommandBuffer>(list.native));
}
#include "VulkanGeometry.inl"
} // namespace souls::rhi
