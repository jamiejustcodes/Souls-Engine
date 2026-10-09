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
void Workspace::release_demo_input() noexcept {
    if (demo_window_)
        if (auto *window = SDL_GetWindowFromID(demo_window_))
            SDL_SetWindowRelativeMouseMode(window, false);
    demo_capture_ = false;
    demo_window_ = 0;
    mouse_x_ = mouse_y_ = 0;
}
void Workspace::show_demo(bool active) noexcept {
    finish_edit();
    if (flying_)
        if (auto *flight = SDL_GetWindowFromID(flight_window_))
            SDL_SetWindowRelativeMouseMode(flight, false);
    flying_ = false;
    marquee_ = false;
    release_demo_input();
    demo_active_ = active;
    demo_focus_ = active;
}
void Workspace::demo_view(ImTextureID texture, SDL_Window *window, float density) noexcept {
    const float dpi = ImGui::GetFontSize() / 15;
    const auto *main = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize({800 * dpi, 540 * dpi}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({main->WorkPos.x + 120 * dpi, main->WorkPos.y + 40 * dpi},
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({360 * dpi, 280 * dpi}, {4096 * dpi, 4096 * dpi});
    if (demo_focus_) {
        ImGui::SetNextWindowFocus();
        demo_focus_ = false;
    }
    const bool visible = ImGui::Begin("Playable Demo", &demo_active_,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (visible && demo_active_) {
        demo_renderable_ = true;
        // The SDL platform handle is a window ID, including detached ImGui viewports.
        auto *target = SDL_GetWindowFromID(static_cast<SDL_WindowID>(
            reinterpret_cast<std::uintptr_t>(ImGui::GetWindowViewport()->PlatformHandle)));
        if (!target)
            target = window;
        const bool blocked = unsaved_prompt_ || files_.pending() || file_error_prompt_ || recovery_available_;
        if (demo_capture_ && (blocked || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                              !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                              demo_window_ != SDL_GetWindowID(target)))
            release_demo_input();
        ImGui::BeginDisabled(blocked);
        ImGui::PushStyleColor(ImGuiCol_Button, {0.12F, 0.39F, 0.26F, 1});
        const auto &before = demo_.state();
        const char *label = demo_capture_   ? "Pause"
                            : before.won    ? "Play again"
                            : demo_started_ ? "Resume"
                                            : "Play";
        if (ImGui::Button(label, {88 * dpi, 0})) {
            if (demo_capture_)
                release_demo_input();
            else {
                if (before.won) {
                    auto reset = demo_.reset();
                    if (!reset)
                        file_error(reset.error().message);
                }
                finish_edit();
                if (flying_)
                    if (auto *flight = SDL_GetWindowFromID(flight_window_))
                        SDL_SetWindowRelativeMouseMode(flight, false);
                flying_ = false;
                demo_capture_ = SDL_SetWindowRelativeMouseMode(target, true);
                if (!demo_capture_)
                    file_error(SDL_GetError());
                else {
                    demo_window_ = SDL_GetWindowID(target);
                    demo_started_ = true;
                    mouse_x_ = mouse_y_ = 0;
                    ImGui::ClearActiveID();
                    ImGui::SetWindowFocus("Playable Demo");
                }
            }
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Restart", {72 * dpi, 0})) {
            auto reset = demo_.reset();
            if (!reset)
                file_error(reset.error().message);
        }
        ImGui::SameLine();
        if (ImGui::Button("Edit demo", {88 * dpi, 0}))
            request_action(Action::edit_demo);
        ImGui::EndDisabled();
        ImGui::TextDisabled("Drag the tab to dock or detach. Esc pauses.");
        auto size = ImGui::GetContentRegionAvail();
        size.x = std::max(1.0F, size.x);
        size.y = std::max(64.0F, size.y - ImGui::GetTextLineHeightWithSpacing());
        const float pixels = SDL_GetWindowPixelDensity(target);
        const float scale = pixels > 0 ? pixels : density;
        demo_extent_ = {static_cast<std::uint32_t>(std::clamp(size.x * scale, 64.0F, 4096.0F)),
                        static_cast<std::uint32_t>(std::clamp(size.y * scale, 64.0F, 4096.0F))};
        ImGui::Image(texture, size);
        const auto origin = ImGui::GetItemRectMin();
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
        if (state.won && demo_capture_)
            release_demo_input();
        auto *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        const ImVec2 hud{origin.x + 12 * dpi, origin.y + 12 * dpi};
        draw->AddRectFilled(hud, {hud.x + 220 * dpi, hud.y + 78 * dpi}, IM_COL32(25, 26, 32, 230), 4 * dpi);
        draw->AddText({hud.x + 12 * dpi, hud.y + 10 * dpi}, IM_COL32(241, 241, 247, 255), "Souls Courtyard");
        char progress[100]{};
        std::snprintf(progress, sizeof(progress), "%u / %u orbs   %s", state.collected, state.total,
                      state.won          ? "Complete"
                      : state.checkpoint ? "Checkpoint"
                                         : "Reach the arch");
        draw->AddText({hud.x + 12 * dpi, hud.y + 32 * dpi}, IM_COL32(218, 222, 232, 255), progress);
        for (std::uint32_t i = 0; i < state.total; ++i)
            draw->AddCircleFilled(
                {hud.x + (18 + static_cast<float>(i) * 20) * dpi, hud.y + 61 * dpi}, 5 * dpi,
                i < state.collected ? IM_COL32(246, 181, 69, 255) : IM_COL32(75, 75, 87, 255));
        if (!demo_capture_) {
            const char *hint =
                state.won ? "Complete. Play again for a faster run." : "Press Play to capture the mouse.";
            const ImVec2 hint_pos{origin.x + 12 * dpi, origin.y + size.y - 38 * dpi};
            draw->AddRectFilled(hint_pos, {hint_pos.x + 276 * dpi, hint_pos.y + 26 * dpi},
                                IM_COL32(25, 26, 32, 230), 4 * dpi);
            draw->AddText({hint_pos.x + 8 * dpi, hint_pos.y + 5 * dpi}, IM_COL32(230, 230, 239, 255), hint);
        }
        draw->PopClipRect();
        ImGui::TextDisabled("WASD move | Space jump | Shift sprint | Mouse look");
    } else
        release_demo_input();
    ImGui::End();
    if (!demo_active_)
        release_demo_input();
}
} // namespace souls::editor
