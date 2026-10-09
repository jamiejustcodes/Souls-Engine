#pragma once
#include "editor/FileDialogs.hpp"
#include <imgui.h>
#include <souls/demo/Demo.hpp>
#include <souls/editor/EditorDocument.hpp>
#include <souls/editor/TransformTools.hpp>
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
    Workspace(EditorDocument &document, Scene &scene, Scene &demo_scene, demo::Session &demo) noexcept
        : document_(document), scene_(scene), demo_scene_(demo_scene), demo_(demo) {}
    void initialize(SDL_Window *window, bool persistent) noexcept;
    void set_logo(ImTextureID logo) noexcept {
        logo_ = logo;
    }
    void show_demo(bool active) noexcept;
    [[nodiscard]] Scene &render_scene() noexcept {
        return demo_active_ ? demo_scene_ : scene_;
    }
    [[nodiscard]] bool demo_active() const noexcept {
        return demo_active_;
    }
    void request_exit() noexcept;
    void draw(ImTextureID viewport, rhi::Extent texture_extent, const char *adapter, Scene &scene,
              SDL_Window *window, float pixel_density) noexcept;
    void event(const SDL_Event &event) noexcept;
    [[nodiscard]] Result<void> verify_workflow(Scene &scene) noexcept;
    [[nodiscard]] const Camera &camera() const noexcept {
        return demo_active_ ? demo_.camera() : camera_;
    }
    [[nodiscard]] EntityHandle selected() const noexcept {
        return demo_active_ ? EntityHandle{} : document_.primary();
    }
    [[nodiscard]] std::span<const EntityHandle> selection() const noexcept {
        return demo_active_ ? std::span<const EntityHandle>{} : document_.selection();
    }
    [[nodiscard]] bool grid() const noexcept {
        return !demo_active_ && grid_;
    }
    [[nodiscard]] bool lit() const noexcept {
        return demo_active_ || lit_;
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
    enum class Action { none, new_level, open_level, edit_demo, quit };
    void viewport(ImTextureID texture, rhi::Extent extent, Scene &scene, SDL_Window *window,
                  float density) noexcept;
    void demo_view(ImTextureID texture, SDL_Window *window, float density) noexcept;
    void draw_brand(float size) noexcept;
    void file_actions() noexcept;
    void file_error(const char *message) noexcept;
    void request_action(Action action) noexcept;
    void perform_action(Action action) noexcept;
    void save_level(bool save_as = false) noexcept;
    void select_entity(EntityHandle entity, SelectionMode mode = SelectionMode::replace) noexcept;
    void finish_edit(bool cancel = false) noexcept;
    void erase_selection() noexcept;
    void undo(bool redo = false) noexcept;
    void begin_transform_edit(const char *label) noexcept;
    void gizmo(const ImVec2 &origin, const ImVec2 &size, float aspect) noexcept;
    void preview_details(const Actor &actor) noexcept;
    bool selection_locked() const noexcept;
    Vec3 selection_center() const noexcept;
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
    EditorDocument &document_;
    Scene &scene_;
    Scene &demo_scene_;
    demo::Session &demo_;
    ImTextureID logo_ = 0;
    bool demo_active_ = true, demo_capture_ = false, demo_started_ = false;
    FileDialogs files_{};
    SDL_Window *main_window_ = nullptr;
    Camera camera_{};
    EntityHandle selected_{};
    std::array<char, 128> actor_search_{}, asset_search_{}, command_{};
    std::array<std::array<char, 160>, 64> log_{};
    std::uint32_t log_cursor_ = 0, log_count_ = 0;
    std::array<Actor, 256> edit_actors_{};
    std::array<EntityHandle, 256> edit_handles_{};
    std::size_t edit_count_ = 0;
    Actor edit_primary_{};
    Mat4 gizmo_start_{}, gizmo_current_{};
    bool gizmo_edit_ = false, details_edit_ = false;
    TransformTool tool_ = TransformTool::move;
    bool local_space_ = false, marquee_ = false;
    ImVec2 marquee_start_{};
    float translate_step_ = 10, rotate_step_ = 15, scale_step_ = 0.25F;
    std::array<char, 1024> recovery_path_{};
    std::array<char, 64> group_name_{"Group"};
    std::array<char, 160> file_error_message_{};
    bool file_error_prompt_ = false, defer_recovery_ = false;
    bool persistent_ = false, recovery_available_ = false, unsaved_prompt_ = false, group_prompt_ = false;
    Action pending_action_ = Action::none, after_save_ = Action::none;
    double autosave_elapsed_ = 0;
    int ribbon_ = 0;
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
