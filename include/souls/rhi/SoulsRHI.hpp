#pragma once
#include <memory>
#include <souls/core/Core.hpp>
#include <souls/core/Math.hpp>

struct SDL_Window;
namespace souls::rhi {
struct BufferTag;
struct TextureTag;
struct PipelineTag;
using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using PipelineHandle = Handle<PipelineTag>;
enum class Backend { d3d12, vulkan };
#if defined(SOULS_RHI_D3D12) == defined(SOULS_RHI_VULKAN)
#error Select exactly one Souls RHI backend through CMake
#endif
#if defined(SOULS_RHI_D3D12)
inline constexpr Backend backend = Backend::d3d12;
#else
inline constexpr Backend backend = Backend::vulkan;
#endif
template <Backend B, class D3D12Fn, class VulkanFn>
decltype(auto) dispatch(D3D12Fn &&dx, VulkanFn &&vk) noexcept {
    if constexpr (B == Backend::d3d12)
        return std::forward<D3D12Fn>(dx)();
    else
        return std::forward<VulkanFn>(vk)();
}
inline constexpr std::uint32_t frames_in_flight = 2;
// Each geometry scope retains independent camera/light constants until frame retirement.
inline constexpr std::uint32_t geometry_scopes_per_frame = 8;
struct Extent {
    std::uint32_t width = 0, height = 0;
    friend bool operator==(Extent, Extent) = default;
};
struct Color {
    float r, g, b, a = 1.0F;
};
enum class Access { undefined, render_target, shader_read, present };
enum class Stage { none, color_output, fragment, all };
struct TextureBarrier {
    TextureHandle texture;
    Stage before, after;
    Access from, to;
};
struct TimelinePoint {
    std::uint64_t value = 0;
};
struct Stats {
    double gpu_us = 0;
    std::uint64_t local_usage = 0, local_budget = 0;
    bool gpu_timing_valid = false, budget_valid = false;
};
struct Swapchain {
    Extent extent;
    std::uint32_t image_count = 0;
};
// Borrowed command token; valid only between begin_frame/end_frame. No vtable.
struct CommandList {
    void *native = nullptr;
    std::uint64_t serial = 0;
};
struct DeviceDesc {
    SDL_Window *window = nullptr;
    bool validation = false;
    bool vsync = true;
};
struct Vertex {
    Vec3 position, normal;
};
enum class BufferUsage { vertex, index };
struct FrameConstants {
    Mat4 view_projection;
    std::array<float, 4> camera, right, up, forward, light_direction, light_color, options, grid_origin,
        grid_u, grid_v, grid_normal, sky_color;
};
static_assert(sizeof(FrameConstants) == 256 && sizeof(Vertex) == 24);
struct DrawConstants {
    Mat4 model;
    std::array<float, 4> color, flags;
};
struct PipelineDesc {
    std::span<const std::byte> vertex_shader, pixel_shader;
    bool grid = false;
};
namespace detail {
struct NativeAccess;
}
class Device final {
  public:
    Device() noexcept;
    ~Device();
    Device(Device &&) noexcept;
    Device &operator=(Device &&) noexcept;
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;
    [[nodiscard]] static Result<Device> create(DeviceDesc desc) noexcept;
    [[nodiscard]] Result<void> resize(Extent pixels) noexcept;
    [[nodiscard]] Result<CommandList> begin_frame(Color clear) noexcept;
    [[nodiscard]] Result<TimelinePoint> end_frame(CommandList list) noexcept;
    [[nodiscard]] Result<void> wait_idle() noexcept;
    [[nodiscard]] Result<void> wait(TimelinePoint point) noexcept;
    [[nodiscard]] Result<TextureHandle> create_render_target(Extent pixels) noexcept;
    // Destruction and creation are cold-path operations; never inside an open frame.
    [[nodiscard]] Result<void> destroy_texture(TextureHandle texture) noexcept;
    [[nodiscard]] Result<void> clear_texture(CommandList list, TextureHandle texture, Color clear) noexcept;
    [[nodiscard]] Result<void> barrier(CommandList list, TextureBarrier barrier) noexcept;
    [[nodiscard]] Result<BufferHandle> create_buffer(std::span<const std::byte> data,
                                                     BufferUsage usage) noexcept;
    [[nodiscard]] Result<void> destroy_buffer(BufferHandle buffer) noexcept;
    [[nodiscard]] Result<PipelineHandle> create_pipeline(PipelineDesc desc) noexcept;
    [[nodiscard]] Result<void> destroy_pipeline(PipelineHandle pipeline) noexcept;
    [[nodiscard]] Result<void> begin_geometry(CommandList list, TextureHandle target,
                                              const FrameConstants &frame) noexcept;
    [[nodiscard]] Result<void> draw_geometry(CommandList list, PipelineHandle pipeline, BufferHandle vertices,
                                             BufferHandle indices, std::uint32_t count,
                                             const DrawConstants &draw) noexcept;
    [[nodiscard]] Result<void> end_geometry(CommandList list, TextureHandle target) noexcept;
    // Cold diagnostic BMP: Windows main composite; Vulkan last offscreen scene color.
    [[nodiscard]] Result<void> capture_frame(const char *path) noexcept;
    [[nodiscard]] Result<void> composite_texture(CommandList list, TextureHandle source) noexcept;
    [[nodiscard]] Swapchain swapchain() const noexcept;
    [[nodiscard]] Stats stats() const noexcept;
    [[nodiscard]] LinearArena &frame_arena() noexcept;
    [[nodiscard]] const char *adapter_name() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend struct detail::NativeAccess;
};
static_assert(!std::is_polymorphic_v<Device> && !std::is_polymorphic_v<CommandList>);
} // namespace souls::rhi
