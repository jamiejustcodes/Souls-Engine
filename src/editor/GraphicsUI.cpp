#include "editor/GraphicsUI.hpp"
#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>
#include <implot.h>
#include <cstdlib>
#include <cstdio>
#if defined(SOULS_RHI_D3D12)
#include <imgui_impl_dx12.h>
#else
#include <imgui_impl_vulkan.h>
#endif
namespace souls::editor {
GraphicsUI::~GraphicsUI() { const auto ignored = shutdown(); (void)ignored; }
Result<void> GraphicsUI::initialize(SDL_Window* window) noexcept {
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImPlot::CreateContext(); context_ = true;
    auto& io = ImGui::GetIO(); io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io.ConfigDpiScaleFonts = true; io.ConfigDpiScaleViewports = true;
    // Use the host's readable UI face without redistributing system fonts.
    const char* font_path = nullptr;
#if defined(_WIN32)
    char windows_font[512]{};
    const auto* windows_root = SDL_getenv("windir");
    if (!windows_root) windows_root = SDL_getenv("WINDIR");
    if (!windows_root) windows_root = SDL_getenv("SystemRoot");
    if (windows_root) { std::snprintf(windows_font, sizeof(windows_font), "%s/Fonts/segoeui.ttf", windows_root); font_path = windows_font; }
#else
    for (const auto* path : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"}) {
        SDL_PathInfo info{}; if (SDL_GetPathInfo(path, &info)) { font_path = path; break; }
    }
#endif
    SDL_PathInfo font_info{};
    if (font_path && SDL_GetPathInfo(font_path, &font_info)) { io.Fonts->AddFontFromFileTTF(font_path, 15.0F); SDL_Log("Editor UI font: %s", font_path); }
    else { ImFontConfig font{}; font.SizePixels = 15; io.Fonts->AddFontDefault(&font); }
    io.IniFilename = nullptr; // Deterministic initial workspace; no writes beside executable.
    const auto native = rhi::detail::NativeAccess::context(device_);
#if defined(SOULS_RHI_D3D12)
    platform_ = ImGui_ImplSDL3_InitForD3D(window);
    ImGui_ImplDX12_InitInfo init{}; init.Device = native.device; init.CommandQueue = native.queue; init.NumFramesInFlight = rhi::frames_in_flight;
    init.RTVFormat = native.format; init.SrvDescriptorHeap = native.srv_heap; init.UserData = &device_;
    init.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto descriptor = rhi::detail::NativeAccess::allocate_descriptor(*static_cast<rhi::Device*>(info->UserData));
        if (!descriptor) { SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "%s", descriptor.error().message); std::abort(); }
        *cpu = descriptor->cpu; *gpu = descriptor->gpu;
    };
    init.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) { rhi::detail::NativeAccess::free_descriptor(*static_cast<rhi::Device*>(info->UserData), {cpu, gpu}); };
    renderer_ = ImGui_ImplDX12_Init(&init);
#else
    platform_ = ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init{}; init.ApiVersion = VK_API_VERSION_1_3; init.Instance = native.instance; init.PhysicalDevice = native.physical;
    init.Device = native.device; init.Queue = native.queue; init.QueueFamily = native.family; init.MinImageCount = native.min_images; init.ImageCount = native.images;
    init.DescriptorPoolSize = 256; init.UseDynamicRendering = true;
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    color_format_ = native.format;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1; init.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &color_format_;
    init.PipelineInfoForViewports = init.PipelineInfoMain;
    init.CheckVkResultFn = [](VkResult result) { if (result < 0) { SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "ImGui Vulkan failure %d", result); std::abort(); } };
    renderer_ = ImGui_ImplVulkan_Init(&init);
    VkSamplerCreateInfo sampler{}; sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; sampler.magFilter = VK_FILTER_LINEAR; sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    auto result = vkCreateSampler(native.device, &sampler, nullptr, &sampler_);
    if (result != VK_SUCCESS) return std::unexpected(Error{ErrorCode::gpu, "UI sampler creation failed", result});
#endif
    if (!platform_ || !renderer_) return std::unexpected(Error{ErrorCode::platform, "ImGui backend initialization failed"});
    return {};
}
void GraphicsUI::new_frame() noexcept {
#if defined(SOULS_RHI_D3D12)
    ImGui_ImplDX12_NewFrame();
#else
    ImGui_ImplVulkan_NewFrame();
#endif
    ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
}
void GraphicsUI::render(rhi::CommandList list) noexcept {
    rhi::detail::NativeAccess::begin_overlay(device_, list);
#if defined(SOULS_RHI_D3D12)
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), static_cast<ID3D12GraphicsCommandList*>(list.native));
#else
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(list.native));
#endif
    rhi::detail::NativeAccess::end_overlay(device_, list);
}
Result<ImTextureID> GraphicsUI::attach(rhi::TextureHandle texture) noexcept {
    if (texture_) return std::unexpected(Error{ErrorCode::invalid_argument, "Detach previous UI texture first"});
    auto view = rhi::detail::NativeAccess::texture(device_, texture); if (!view) return std::unexpected(view.error());
#if defined(SOULS_RHI_D3D12)
    texture_ = view->gpu.ptr;
#else
    texture_ = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(sampler_, view->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    if (!texture_) return std::unexpected(Error{ErrorCode::exhausted, "UI texture descriptor allocation failed"});
#endif
    return texture_;
}
Result<void> GraphicsUI::detach() noexcept {
    if (!texture_) return {};
    if (auto idle = device_.wait_idle(); !idle) return idle;
#if defined(SOULS_RHI_VULKAN)
    ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(texture_));
#endif
    texture_ = 0; return {};
}
Result<void> GraphicsUI::shutdown() noexcept {
    if (!context_) return {};
    if (auto idle = device_.wait_idle(); !idle) return idle;
    if (auto detached = detach(); !detached) return detached;
    // The renderer backend first releases its main-viewport data and then
    // destroys secondary platform windows. Destroying them earlier would send
    // the application's main viewport through the secondary-swapchain teardown.
    if (renderer_) {
#if defined(SOULS_RHI_D3D12)
        ImGui_ImplDX12_Shutdown();
#else
        ImGui_ImplVulkan_Shutdown();
#endif
        renderer_ = false;
    }
#if defined(SOULS_RHI_VULKAN)
    if (sampler_) { vkDestroySampler(rhi::detail::NativeAccess::context(device_).device, sampler_, nullptr); sampler_ = VK_NULL_HANDLE; }
#endif
    if (platform_) { ImGui_ImplSDL3_Shutdown(); platform_ = false; }
    ImPlot::DestroyContext(); ImGui::DestroyContext(); context_ = false;
    return {};
}
}
