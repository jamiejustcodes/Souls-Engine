#pragma once
#include <imgui.h>
#include <souls/rhi/SoulsRHI.hpp>
#include <souls/scene/Scene.hpp>
union SDL_Event;
struct SDL_Window;
namespace souls::editor {
struct Sample {
    double frame_ms = 0;
    std::array<double, 4> cpu_us{};
    rhi::Stats gpu{};
    std::uint32_t draws = 0, scene_draws = 0;
    std::size_t arena_bytes = 0;
};
class Workspace final {
  public:
    void draw(ImTextureID viewport, rhi::Extent texture_extent, const char *adapter, Scene &scene,
              SDL_Window *window, float pixel_density) noexcept;
    void event(const SDL_Event &event) noexcept;
    [[nodiscard]] Result<void> verify_workflow(Scene &scene) noexcept;
    [[nodiscard]] const Camera &camera() const noexcept {
        return camera_;
    }
    [[nodiscard]] EntityHandle selected() const noexcept {
        return selected_;
    }
    [[nodiscard]] bool grid() const noexcept {
        return grid_;
    }
    [[nodiscard]] bool lit() const noexcept {
        return lit_;
    }
    void push(Sample sample) noexcept;
    [[nodiscard]] rhi::Extent requested_extent() const noexcept {
        return requested_;
    }
    [[nodiscard]] bool paused() const noexcept {
        return paused_;
    }
    [[nodiscard]] bool quit_requested() const noexcept {
        return quit_;
    }
    static void theme(float scale) noexcept;

  private:
    void telemetry() noexcept;
    void graph() noexcept;
    void outliner(Scene &scene) noexcept;
    void details(Scene &scene) noexcept;
    void content(Scene &scene) noexcept;
    void console(Scene &scene) noexcept;
    void spawn(Scene &scene, ActorKind kind) noexcept;
    void duplicate_actor(Scene &scene, EntityHandle entity) noexcept;
    void log(const char *text) noexcept;
    void play(Scene &scene) noexcept;
    void stop(Scene &scene) noexcept;
    Camera camera_{};
    EntityHandle selected_{};
    std::array<char, 128> actor_search_{}, asset_search_{}, command_{};
    std::array<std::array<char, 160>, 64> log_{};
    std::uint32_t log_cursor_ = 0, log_count_ = 0;
    std::array<Actor, 256> snapshot_{};
    std::array<EntityHandle, 256> snapshot_handles_{};
    std::size_t snapshot_count_ = 0;
    float mouse_x_ = 0, mouse_y_ = 0;
    std::uint32_t flight_window_ = 0;
    bool playing_ = false, ejected_ = false, flying_ = false, grid_ = true, lit_ = true,
         show_outliner_ = true, show_details_ = true, show_content_ = true, show_tools_ = true;
    int mode_ = 0, folder_ = 0, asset_ = -1;
    bool rename_ = false;
    bool focus_log_ = false;
    bool focus_content_ = true;
    bool translate_snap_ = false, rotate_snap_ = false, scale_snap_ = false;
    std::array<float, 240> cpu_ms_{}, gpu_ms_{};
    std::uint32_t cursor_ = 0, count_ = 0;
    Sample last_{};
    rhi::Extent requested_{960, 540};
    std::array<ImVec2, 3> nodes_{{{24, 48}, {304, 48}, {584, 48}}};
    ImVec2 pan_{16, 16};
    float zoom_ = 1;
    int budget_ = 0, pass_ = 0, selected_node_ = -1;
    bool paused_ = false, quit_ = false, layout_ = false, fit_graph_ = true;
};
} // namespace souls::editor
