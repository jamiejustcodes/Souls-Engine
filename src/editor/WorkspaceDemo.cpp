#include "editor/Workspace.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <imgui_internal.h>
namespace souls::editor {
void Workspace::draw_brand(float size) noexcept {
    const auto p = ImGui::GetCursorScreenPos();
    if (logo_) {
        // Crop only at presentation: the complete, unmodified supplied artwork is embedded.
        ImGui::GetWindowDrawList()->AddImageRounded(logo_, p, {p.x + size, p.y + size},
                                                    {352.0F / 1024, 113.0F / 687},
                                                    {672.0F / 1024, 433.0F / 687}, IM_COL32_WHITE, size / 2);
    }
    ImGui::Dummy({size, size});
}
void Workspace::show_demo(bool active) noexcept {
    finish_edit();
    if (playing_)
        stop(scene_);
    if (flying_)
        if (auto *flight = SDL_GetWindowFromID(flight_window_))
            SDL_SetWindowRelativeMouseMode(flight, false);
    if (main_window_)
        SDL_SetWindowRelativeMouseMode(main_window_, false);
    flying_ = demo_capture_ = false;
    mouse_x_ = mouse_y_ = 0;
    demo_active_ = active;
    if (!active)
        focus_content_ = false;
}
void Workspace::demo_view(ImTextureID texture, SDL_Window *window, float density) noexcept {
    const float dpi = ImGui::GetFontSize() / 15;
    auto *vp = ImGui::GetMainViewport();
    if (ImGui::BeginViewportSideBar("Demo toolbar", vp, ImGuiDir_Up, 60 * dpi,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        draw_brand(36 * dpi);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Souls Engine");
        ImGui::TextDisabled("Souls Courtyard / Playable demo");
        ImGui::EndGroup();
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 412 * dpi));
        if (ImGui::Button("Open editor", {112 * dpi, 32 * dpi}))
            show_demo(false);
        ImGui::SameLine();
        if (ImGui::Button("Edit demo", {96 * dpi, 32 * dpi}))
            request_action(Action::edit_demo);
        ImGui::SameLine();
        if (ImGui::Button("Restart", {88 * dpi, 32 * dpi})) {
            auto done = demo_.reset();
            if (!done)
                file_error(done.error().message);
        }
        ImGui::SameLine();
        if (ImGui::Button("About", {72 * dpi, 32 * dpi}))
            ImGui::OpenPopup("Demo about");
        if (ImGui::BeginPopup("Demo about")) {
            ImGui::Image(logo_, {304 * dpi, 204 * dpi});
            ImGui::TextWrapped(
                "A playable courtyard built with Souls Engine's primitive meshes and kinematic controller.");
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("Souls Courtyard", nullptr, flags)) {
        auto size = ImGui::GetContentRegionAvail();
        size.x = std::max(size.x, 1.0F);
        size.y = std::max(size.y, 1.0F);
        requested_ = {static_cast<std::uint32_t>(std::clamp(size.x * density, 64.0F, 4096.0F)),
                      static_cast<std::uint32_t>(std::clamp(size.y * density, 64.0F, 4096.0F))};
        ImGui::Image(texture, size);
        const auto p = ImGui::GetItemRectMin();
        const bool hovered = ImGui::IsItemHovered();
        if (demo_capture_ && (ImGui::IsKeyPressed(ImGuiKey_Escape) || !demo_active_)) {
            demo_capture_ = false;
            SDL_SetWindowRelativeMouseMode(window, false);
        }
        const bool blocked = unsaved_prompt_ || files_.pending() || file_error_prompt_ || recovery_available_;
        if (blocked && demo_capture_) {
            demo_capture_ = false;
            SDL_SetWindowRelativeMouseMode(window, false);
        }
        // The SDL adapter supplies input; Session owns all movement and collision rules.
        demo::Input input{};
        if (demo_capture_ && !blocked) {
            input.forward = static_cast<float>(ImGui::IsKeyDown(ImGuiKey_W)) -
                            static_cast<float>(ImGui::IsKeyDown(ImGuiKey_S));
            input.right = static_cast<float>(ImGui::IsKeyDown(ImGuiKey_D)) -
                          static_cast<float>(ImGui::IsKeyDown(ImGuiKey_A));
            input.jump = ImGui::IsKeyDown(ImGuiKey_Space);
            input.sprint = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
            input.look_yaw = -mouse_x_ * 0.003F;
            input.look_pitch = -mouse_y_ * 0.003F;
        }
        auto done = demo_.tick(input, demo_capture_ && !blocked ? ImGui::GetIO().DeltaTime : 0);
        mouse_x_ = mouse_y_ = 0;
        if (!done)
            file_error(done.error().message);
        const auto &state = demo_.state();
        if (state.won && demo_capture_) {
            demo_capture_ = false;
            SDL_SetWindowRelativeMouseMode(window, false);
        }
        auto *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(p, {p.x + size.x, p.y + size.y}, true);
        const ImVec2 hud{p.x + 24 * dpi, p.y + 24 * dpi};
        draw->AddRectFilled(hud, {hud.x + 328 * dpi, hud.y + 122 * dpi}, IM_COL32(25, 26, 32, 232), 4 * dpi);
        draw->AddText({hud.x + 16 * dpi, hud.y + 12 * dpi}, IM_COL32(241, 241, 247, 255), "Souls Courtyard");
        draw->AddText({hud.x + 16 * dpi, hud.y + 35 * dpi}, IM_COL32(190, 194, 207, 255),
                      "Collect four orbs. Reach the final arch.");
        for (std::uint32_t i = 0; i < state.total; ++i) {
            const ImVec2 orb{hud.x + (24 + static_cast<float>(i) * 22) * dpi, hud.y + 74 * dpi};
            draw->AddCircleFilled(
                orb, 6 * dpi, i < state.collected ? IM_COL32(246, 181, 69, 255) : IM_COL32(75, 75, 87, 255));
        }
        char progress[120]{};
        std::snprintf(progress, sizeof(progress), "%u / %u orbs    %s", state.collected, state.total,
                      state.checkpoint ? "Checkpoint reached" : "Follow the gold beacons");
        draw->AddText({hud.x + 16 * dpi, hud.y + 97 * dpi}, IM_COL32(218, 222, 232, 255), progress);
        char timing[100]{};
        const auto seconds = static_cast<unsigned>(state.elapsed);
        std::snprintf(timing, sizeof(timing), "%02u:%02u   %u falls", seconds / 60, seconds % 60,
                      state.falls);
        draw->AddText({p.x + size.x - 164 * dpi, p.y + 24 * dpi}, IM_COL32(226, 229, 237, 255), timing);
        draw->AddRectFilled({p.x, p.y + size.y - 42 * dpi}, {p.x + size.x, p.y + size.y},
                            IM_COL32(25, 26, 32, 230));
        draw->AddText({p.x + 24 * dpi, p.y + size.y - 29 * dpi}, IM_COL32(203, 208, 220, 255),
                      demo_capture_ ? "WASD move     Space jump     Shift sprint     Mouse look     Esc pause"
                                    : "Playable demo     Click Start playing to capture the mouse");
        char fps[64]{};
        std::snprintf(fps, sizeof(fps), "%s    %.0f FPS",
                      rhi::backend == rhi::Backend::d3d12 ? "D3D12" : "Vulkan",
                      last_.frame_ms > 0 ? 1000 / last_.frame_ms : 0);
        draw->AddText({p.x + size.x - 180 * dpi, p.y + size.y - 29 * dpi}, IM_COL32(167, 175, 194, 255), fps);
        draw->PopClipRect();
        if (!demo_capture_) {
            const float panel_width = 384 * dpi;
            ImGui::SetCursorScreenPos({p.x + (size.x - panel_width) / 2, p.y + size.y - 202 * dpi});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20 * dpi, 16 * dpi});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.10F, 0.10F, 0.13F, 0.96F});
            if (ImGui::BeginChild("Demo controls", {panel_width, 146 * dpi},
                                  ImGuiChildFlags_AlwaysUseWindowPadding)) {
                ImGui::TextUnformatted(state.won       ? "Courtyard complete"
                                       : demo_started_ ? "Paused"
                                                       : "Your first Souls scene");
                ImGui::TextDisabled(state.won ? "All four orbs collected. Try a faster run."
                                              : "Walk, jump and explore the floating courtyard.");
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, {0.12F, 0.39F, 0.26F, 1});
                ImGui::BeginDisabled(blocked);
                if (ImGui::Button(state.won       ? "Play again"
                                  : demo_started_ ? "Resume"
                                                  : "Start playing",
                                  {-1, 32 * dpi})) {
                    if (state.won) {
                        auto restarted = demo_.reset();
                        if (!restarted)
                            file_error(restarted.error().message);
                    }
                    demo_capture_ = SDL_SetWindowRelativeMouseMode(window, true);
                    if (!demo_capture_)
                        file_error(SDL_GetError());
                    else {
                        demo_started_ = true;
                        ImGui::SetWindowFocus("Souls Courtyard");
                    }
                }
                ImGui::EndDisabled();
                ImGui::PopStyleColor();
                ImGui::TextDisabled("The editor remains available above.");
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        } else if (hovered && ImGui::IsMouseClicked(0))
            ImGui::SetWindowFocus("Souls Courtyard");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
} // namespace souls::editor
