#pragma once
#include "rhi/NativeAccess.hpp"
#include <array>
#include <imgui.h>
#include <souls/brand/Brand.hpp>
namespace souls::editor {
class GraphicsUI final {
  public:
    explicit GraphicsUI(rhi::Device &device) noexcept : device_(device) {}
    ~GraphicsUI();
    [[nodiscard]] Result<void> initialize(SDL_Window *window, const brand::Logo &logo) noexcept;
    [[nodiscard]] ImTextureID logo() const noexcept {
        return logo_.GetTexID();
    }
    void new_frame() noexcept;
    void render(rhi::CommandList list) noexcept;
    [[nodiscard]] Result<ImTextureID> attach(rhi::TextureHandle texture) noexcept;
    [[nodiscard]] Result<void> detach() noexcept;
    [[nodiscard]] Result<void> detach(ImTextureID texture) noexcept;
    [[nodiscard]] Result<void> shutdown() noexcept;

  private:
    rhi::Device &device_;
    bool context_ = false, platform_ = false, renderer_ = false;
    std::array<ImTextureID, 2> textures_{};
    ImTextureData logo_{};
#if defined(SOULS_RHI_VULKAN)
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkFormat color_format_ = VK_FORMAT_UNDEFINED;
#endif
};
} // namespace souls::editor
