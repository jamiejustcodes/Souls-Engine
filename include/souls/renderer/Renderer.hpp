#pragma once
#include <souls/rhi/SoulsRHI.hpp>
#include <souls/scene/Scene.hpp>
namespace souls {
class Renderer final {
  public:
    explicit Renderer(rhi::Device &device) noexcept : device_(device) {}
    [[nodiscard]] Result<void> initialize() noexcept;
    [[nodiscard]] Result<void> verify_resources() noexcept;
    [[nodiscard]] Result<void> resize_viewport(rhi::Extent extent) noexcept;
    [[nodiscard]] Result<void> record(rhi::CommandList list, const Scene &scene, const RenderBatch &batch,
                                      const Camera &camera, EntityHandle selected = {}, bool grid = true,
                                      bool lit = true) noexcept;
    [[nodiscard]] std::uint32_t draws() const noexcept {
        return draws_;
    }
    [[nodiscard]] rhi::TextureHandle viewport() const noexcept {
        return target_;
    }
    [[nodiscard]] rhi::Extent extent() const noexcept {
        return extent_;
    }
    [[nodiscard]] Result<void> shutdown() noexcept;

  private:
    rhi::Device &device_;
    rhi::TextureHandle target_{};
    rhi::Extent extent_{};
    struct Mesh {
        rhi::BufferHandle vertices{}, indices{};
        std::uint32_t count = 0;
    };
    std::array<Mesh, 2> meshes_{};
    rhi::PipelineHandle mesh_pipeline_{}, grid_pipeline_{};
    std::uint32_t draws_ = 0;
};
} // namespace souls
