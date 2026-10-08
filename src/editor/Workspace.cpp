// Keep workspace layout and presentation here; actor editing lives in WorkspaceActors.cpp.
#include "editor/Workspace.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui_internal.h>
#include <implot.h>
namespace souls::editor {
namespace {
ImVec4 rgb(unsigned r, unsigned g, unsigned b) noexcept {
    return {static_cast<float>(r) / 255, static_cast<float>(g) / 255, static_cast<float>(b) / 255, 1};
}
ImVec2 add(ImVec2 a, ImVec2 b) noexcept {
    return {a.x + b.x, a.y + b.y};
}
ImVec2 mul(ImVec2 a, float b) noexcept {
    return {a.x * b, a.y * b};
}
} // namespace
void Workspace::theme(float scale) noexcept {
    ImGuiStyle style{};
    style.WindowPadding = {8, 8};
    style.FramePadding = {8, 4};
    style.ItemSpacing = {8, 8};
    style.ItemInnerSpacing = {8, 4};
    style.WindowRounding = 0;
    style.ChildRounding = 0;
    style.FrameRounding = 4;
    style.GrabRounding = 4;
    style.TabRounding = 4;
    style.ScrollbarSize = 12;
    auto *c = style.Colors;
    c[ImGuiCol_Text] = rgb(240, 240, 245);
    c[ImGuiCol_TextDisabled] = rgb(185, 187, 200);
    c[ImGuiCol_WindowBg] = rgb(30, 30, 36);
    c[ImGuiCol_ChildBg] = rgb(30, 30, 36);
    c[ImGuiCol_PopupBg] = rgb(43, 43, 54);
    c[ImGuiCol_Border] = rgb(63, 63, 77);
    c[ImGuiCol_FrameBg] = rgb(43, 43, 54);
    c[ImGuiCol_FrameBgHovered] = rgb(57, 57, 69);
    c[ImGuiCol_FrameBgActive] = rgb(75, 65, 128);
    c[ImGuiCol_TitleBg] = rgb(30, 30, 36);
    c[ImGuiCol_TitleBgActive] = rgb(43, 43, 54);
    c[ImGuiCol_MenuBarBg] = rgb(43, 43, 54);
    c[ImGuiCol_Button] = rgb(57, 57, 69);
    c[ImGuiCol_ButtonHovered] = rgb(92, 78, 181);
    c[ImGuiCol_ButtonActive] = rgb(108, 92, 231);
    c[ImGuiCol_Header] = rgb(66, 58, 112);
    c[ImGuiCol_HeaderHovered] = rgb(85, 73, 156);
    c[ImGuiCol_HeaderActive] = rgb(108, 92, 231);
    c[ImGuiCol_Tab] = rgb(43, 43, 54);
    c[ImGuiCol_TabSelected] = rgb(66, 58, 112);
    c[ImGuiCol_TabHovered] = rgb(85, 73, 156);
    c[ImGuiCol_TabDimmed] = rgb(35, 35, 43);
    c[ImGuiCol_TabDimmedSelected] = rgb(55, 51, 79);
    c[ImGuiCol_CheckMark] = rgb(160, 146, 255);
    c[ImGuiCol_SliderGrab] = rgb(160, 146, 255);
    c[ImGuiCol_SliderGrabActive] = rgb(185, 173, 255);
    c[ImGuiCol_Separator] = rgb(63, 63, 77);
    c[ImGuiCol_SeparatorHovered] = rgb(108, 92, 231);
    c[ImGuiCol_SeparatorActive] = rgb(160, 146, 255);
    c[ImGuiCol_DockingPreview] = {108.0F / 255, 92.0F / 255, 231.0F / 255, 0.65F};
    c[ImGuiCol_DockingEmptyBg] = rgb(30, 30, 36);
    c[ImGuiCol_PlotLines] = rgb(160, 146, 255);
    c[ImGuiCol_PlotHistogram] = rgb(108, 92, 231);
    c[ImGuiCol_NavCursor] = rgb(185, 173, 255);
    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
}
void Workspace::push(Sample sample) noexcept {
    last_ = sample;
    cpu_ms_[cursor_] = static_cast<float>(sample.frame_ms);
    gpu_ms_[cursor_] = static_cast<float>(sample.gpu.gpu_us / 1000);
    cursor_ = (cursor_ + 1) % static_cast<std::uint32_t>(cpu_ms_.size());
    count_ = std::min(count_ + 1, static_cast<std::uint32_t>(cpu_ms_.size()));
}
void Workspace::draw(ImTextureID texture, rhi::Extent extent, const char *adapter, Scene &scene,
                     SDL_Window *window, float pixel_density) noexcept {
    const float dpi = ImGui::GetFontSize() / 15;
    if (flying_ && (!ImGui::IsMouseDown(ImGuiMouseButton_Right) || (playing_ && !ejected_))) {
        if (auto *w = SDL_GetWindowFromID(flight_window_))
            SDL_SetWindowRelativeMouseMode(w, false);
        flying_ = false;
    }
    if (!ImGui::GetIO().WantTextInput && selected_) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D)) {
            duplicate_actor(scene, selected_);
        }
        if (ImGui::Shortcut(ImGuiKey_Delete)) {
            auto done = scene.remove(selected_);
            if (done)
                selected_ = {};
            else
                log(done.error().message);
        }
    }
    if (ImGui::BeginMainMenuBar()) {
        ImGui::TextColored(rgb(169, 156, 255), "SOULS");
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Reset camera"))
                camera_ = Camera{};
            if (ImGui::MenuItem("Exit"))
                quit_ = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            ImGui::BeginDisabled(!selected_);
            if (ImGui::MenuItem("Duplicate actor", "Ctrl+D")) {
                duplicate_actor(scene, selected_);
            }
            if (ImGui::MenuItem("Delete actor", "Delete")) {
                auto done = scene.remove(selected_);
                if (done)
                    selected_ = {};
                else
                    log(done.error().message);
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("World Outliner", nullptr, &show_outliner_);
            ImGui::MenuItem("Details", nullptr, &show_details_);
            ImGui::MenuItem("Content Browser", nullptr, &show_content_);
            ImGui::MenuItem("Developer tools", nullptr, &show_tools_);
            if (ImGui::MenuItem("Reset layout"))
                layout_ = false;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Telemetry / Render Graph / Output Log"))
                show_tools_ = true;
            if (ImGui::MenuItem("Focus selected actor", "F") && selected_) {
                auto a = scene.actor(selected_);
                if (a)
                    camera_.focus({a->transform.x, a->transform.y, a->transform.z});
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    auto *vp = ImGui::GetMainViewport();
    if (ImGui::BeginViewportSideBar("Main toolbar", vp, ImGuiDir_Up, 48 * dpi,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::SetNextItemWidth(145 * dpi);
        ImGui::Combo("##mode", &mode_, "Selection\0Landscape\0Modeling\0");
        ImGui::SameLine();
        if (ImGui::Button("+ Add actor"))
            ImGui::OpenPopup("quick_add");
        if (ImGui::BeginPopup("quick_add")) {
            if (ImGui::MenuItem("Cube"))
                spawn(scene, ActorKind::cube);
            if (ImGui::MenuItem("Sphere"))
                spawn(scene, ActorKind::sphere);
            if (ImGui::MenuItem("Directional Light"))
                spawn(scene, ActorKind::light);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("DefaultMap");
        ImGui::SameLine(std::max(430 * dpi, ImGui::GetWindowWidth() * 0.43F));
        ImGui::PushStyleColor(ImGuiCol_Button, {0.10F, 0.37F, 0.22F, 1});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.14F, 0.48F, 0.29F, 1});
        ImGui::BeginDisabled(playing_);
        if (ImGui::Button("Play", {64 * dpi, 0}))
            play(scene);
        ImGui::EndDisabled();
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::BeginDisabled(!playing_);
        if (ImGui::Button(paused_ ? "Resume" : "Pause"))
            paused_ = !paused_;
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
            stop(scene);
        ImGui::SameLine();
        if (ImGui::Button(ejected_ ? "Possess" : "Eject"))
            ejected_ = !ejected_;
        ImGui::EndDisabled();
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 300 * dpi));
        if (ImGui::Button("Settings"))
            ImGui::OpenPopup("settings");
        if (ImGui::BeginPopup("settings")) {
            ImGui::Checkbox("Grid", &grid_);
            ImGui::Checkbox("Lit shading", &lit_);
            ImGui::Combo("Frame budget", &budget_,
                         "60 Hz\0"
                         "144 Hz\0");
            if (ImGui::Button("Reset camera"))
                camera_ = Camera{};
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Platforms"))
            ImGui::OpenPopup("platforms");
        if (ImGui::BeginPopup("platforms")) {
            ImGui::Text("Active: %s",
                        rhi::backend == rhi::Backend::d3d12 ? "Windows / D3D12" : "Linux / Vulkan 1.3");
            ImGui::TextWrapped("%s", adapter);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::TextColored(playing_ ? rgb(94, 206, 132) : rgb(160, 146, 255), "%s",
                           playing_ ? (paused_ ? "Paused" : "Simulating") : "Ready");
    }
    ImGui::End();
    if (ImGui::BeginViewportSideBar("Status bar", vp, ImGuiDir_Down, 30 * dpi,
                                    ImGuiWindowFlags_NoScrollbar)) {
        if (ImGui::Button(show_content_ ? "Content Drawer  v" : "Content Drawer  ^")) {
            show_content_ = !show_content_;
            focus_content_ = show_content_;
        }
        ImGui::SameLine();
        if (ImGui::Button("Output Log")) {
            show_tools_ = true;
            focus_log_ = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu actors  |  %s", scene.size(),
                            rhi::backend == rhi::Backend::d3d12 ? "D3D12" : "Vulkan 1.3");
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 300 * dpi));
        ImGui::Text("%.2f ms  /  %.0f FPS  |  %u draws", last_.frame_ms,
                    last_.frame_ms > 0 ? 1000 / last_.frame_ms : 0, last_.scene_draws);
    }
    ImGui::End();
    const auto dock = ImGui::DockSpaceOverViewport();
    if (!layout_) {
        layout_ = true;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, vp->WorkSize);
        ImGuiID center = dock;
        const auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.27F, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24F, nullptr, &center);
        const auto details_dock = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.58F, nullptr, &right);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderDockWindow("World Outliner", right);
        ImGui::DockBuilderDockWindow("Details", details_dock);
        for (auto title : {"Content Browser", "Telemetry", "Render graph", "Output Log"})
            ImGui::DockBuilderDockWindow(title, bottom);
        ImGui::DockBuilderFinish(dock);
    }
    if (ImGui::Begin("Viewport", nullptr,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (ImGui::Button("Perspective v"))
            ImGui::OpenPopup("view");
        if (ImGui::BeginPopup("view")) {
            if (ImGui::MenuItem("Perspective / Reset"))
                camera_ = Camera{};
            if (ImGui::MenuItem("Top")) {
                camera_.position = {0, 0, 14};
                camera_.pitch = -1.56F;
                camera_.yaw = pi / 2;
            }
            if (ImGui::MenuItem("Front")) {
                camera_.position = {0, -14, 3};
                camera_.pitch = 0;
                camera_.yaw = pi / 2;
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(lit_ ? "Lit v" : "Unlit v"))
            lit_ = !lit_;
        ImGui::SameLine();
        if (ImGui::Button("Show v"))
            ImGui::OpenPopup("show");
        if (ImGui::BeginPopup("show")) {
            ImGui::Checkbox("Grid and floor", &grid_);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80 * dpi);
        ImGui::SliderFloat("Speed", &camera_.speed, 1, 8, "%.1f");
        ImGui::SameLine();
        ImGui::Checkbox("T 10", &translate_snap_);
        ImGui::SameLine();
        ImGui::Checkbox("R 15", &rotate_snap_);
        ImGui::SameLine();
        ImGui::Checkbox("S .25", &scale_snap_);
        if (mode_ == 1) {
            ImGui::TextDisabled("Landscape / procedural floor");
            ImGui::SameLine();
            ImGui::Checkbox("Grid visible", &grid_);
            ImGui::SameLine();
            if (ImGui::SmallButton("Select floor"))
                for (auto h : scene.actors()) {
                    auto a = scene.actor(h);
                    if (a->kind == ActorKind::floor)
                        selected_ = h;
                }
        }
        if (mode_ == 2) {
            ImGui::TextDisabled("Modeling / primitives");
            ImGui::SameLine();
            if (ImGui::SmallButton("Add Cube"))
                spawn(scene, ActorKind::cube);
            ImGui::SameLine();
            if (ImGui::SmallButton("Add Sphere"))
                spawn(scene, ActorKind::sphere);
        }
        auto size = ImGui::GetContentRegionAvail();
        size.x = std::max(1.0F, size.x);
        size.y = std::max(1.0F, size.y);
        auto *platform_window = SDL_GetWindowFromID(static_cast<SDL_WindowID>(
            reinterpret_cast<std::uintptr_t>(ImGui::GetWindowViewport()->PlatformHandle)));
        float density = platform_window ? SDL_GetWindowPixelDensity(platform_window) : pixel_density;
        requested_ = {static_cast<std::uint32_t>(std::clamp(size.x * density, 64.0F, 4096.0F)),
                      static_cast<std::uint32_t>(std::clamp(size.y * density, 64.0F, 4096.0F))};
        ImGui::Image(texture, size);
        const auto p = ImGui::GetItemRectMin();
        bool hovered = ImGui::IsItemHovered();
        auto &io = ImGui::GetIO();
        if ((!playing_ || ejected_) && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::ClearActiveID();
            ImGui::SetWindowFocus("Viewport");
            auto *target = platform_window ? platform_window : window;
            flight_window_ = SDL_GetWindowID(target);
            flying_ = SDL_SetWindowRelativeMouseMode(target, true);
            if (!flying_)
                log(SDL_GetError());
        }
        if (flying_ && (!ImGui::IsMouseDown(ImGuiMouseButton_Right) || (playing_ && !ejected_))) {
            flying_ = false;
            SDL_SetWindowRelativeMouseMode(platform_window ? platform_window : window, false);
        }
        if (flying_) {
            camera_.yaw = std::remainder(camera_.yaw - mouse_x_ * 0.003F, 2 * pi);
            camera_.pitch = std::clamp(camera_.pitch - mouse_y_ * 0.003F, -1.56F, 1.56F);
            float dt = std::min(io.DeltaTime, 0.1F),
                  speed = camera_.speed * dt * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 6 : 2);
            Vec3 move{};
            if (ImGui::IsKeyDown(ImGuiKey_W))
                move = move + camera_.forward();
            if (ImGui::IsKeyDown(ImGuiKey_S))
                move = move - camera_.forward();
            if (ImGui::IsKeyDown(ImGuiKey_D))
                move = move + camera_.right();
            if (ImGui::IsKeyDown(ImGuiKey_A))
                move = move - camera_.right();
            if (ImGui::IsKeyDown(ImGuiKey_E))
                move.z += 1;
            if (ImGui::IsKeyDown(ImGuiKey_Q))
                move.z -= 1;
            camera_.position = camera_.position + normalize(move) * speed;
        }
        mouse_x_ = mouse_y_ = 0;
        if (hovered) {
            camera_.speed = std::clamp(camera_.speed * std::pow(1.2F, io.MouseWheel), 1.0F, 8.0F);
            const bool on_gizmo = io.MousePos.x > p.x + size.x - 98 * dpi && io.MousePos.y < p.y + 108 * dpi;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !on_gizmo) {
                float x = (io.MousePos.x - p.x) / size.x * 2 - 1, y = 1 - (io.MousePos.y - p.y) / size.y * 2;
                selected_ = scene.pick(
                    camera_,
                    camera_.ray(x, y, static_cast<float>(extent.width) / static_cast<float>(extent.height)));
            }
            if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F) && selected_) {
                auto a = scene.actor(selected_);
                if (a)
                    camera_.focus({a->transform.x, a->transform.y, a->transform.z});
            }
        }
        auto *draw = ImGui::GetWindowDrawList();
        if (selected_) {
            auto a = scene.actor(selected_);
            if (a && a->visible && (a->kind == ActorKind::cube || a->kind == ActorKind::sphere)) {
                const auto &t = a->transform;
                const auto matrix =
                    camera_.projection(static_cast<float>(extent.width) / static_cast<float>(extent.height)) *
                    camera_.view() * model_matrix({t.x, t.y, t.z}, t.rotation, t.scale);
                std::array<ImVec2, 8> corners{};
                bool in_front = true;
                for (std::size_t i = 0; i < 8; ++i) {
                    Vec3 local{(i & 1) ? 1.0F : -1.0F, (i & 2) ? 1.0F : -1.0F, (i & 4) ? 1.0F : -1.0F};
                    const auto &m = matrix.m;
                    float w = m[3] * local.x + m[7] * local.y + m[11] * local.z + m[15];
                    if (w <= 0.1F)
                        in_front = false;
                    auto ndc = transform_point(matrix, local);
                    corners[i] = {p.x + (ndc.x + 1) * size.x / 2, p.y + (1 - ndc.y) * size.y / 2};
                }
                if (in_front) {
                    draw->PushClipRect(p, add(p, size), true);
                    for (std::size_t i = 0; i < 8; ++i)
                        for (auto bit : {1U, 2U, 4U})
                            if (!(i & bit))
                                draw->AddLine(corners[i], corners[i | bit], IM_COL32(255, 174, 63, 220),
                                              1 * dpi);
                    draw->PopClipRect();
                }
            }
        }
        const ImVec2 axis_origin{p.x + size.x - 54 * dpi, p.y + 64 * dpi};
        const Vec3 axes[]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        const ImU32 colors[]{IM_COL32(238, 84, 79, 255), IM_COL32(101, 207, 131, 255),
                             IM_COL32(82, 149, 244, 255)};
        const char *labels[]{"X", "Y", "Z"};
        draw->AddRectFilled({axis_origin.x - 44 * dpi, axis_origin.y - 46 * dpi},
                            {axis_origin.x + 44 * dpi, axis_origin.y + 44 * dpi}, IM_COL32(22, 24, 29, 190),
                            6 * dpi);
        for (int i = 0; i < 3; ++i) {
            ImVec2 end{axis_origin.x + dot(axes[i], camera_.right()) * 30 * dpi,
                       axis_origin.y - dot(axes[i], camera_.up()) * 30 * dpi};
            draw->AddLine(axis_origin, end, colors[i], 2 * dpi);
            draw->AddCircleFilled(end, 8 * dpi, colors[i]);
            draw->AddText({end.x - 4 * dpi, end.y - 8 * dpi}, IM_COL32(20, 22, 27, 255), labels[i]);
            const float dx = io.MousePos.x - end.x, dy = io.MousePos.y - end.y;
            if (hovered && dx * dx + dy * dy < 100 * dpi * dpi) {
                ImGui::SetTooltip("Click to align %s view", labels[i]);
                if (ImGui::IsMouseClicked(0)) {
                    Vec3 center{};
                    if (auto a = scene.actor(selected_); a)
                        center = {a->transform.x, a->transform.y, a->transform.z};
                    camera_.position = center + axes[i] * 12 + Vec3{0, 0, i == 2 ? 0.0F : 3.0F};
                    camera_.yaw = i == 0 ? pi : (i == 1 ? -pi / 2 : pi / 2);
                    camera_.pitch = i == 2 ? -1.56F : -std::atan(0.25F);
                }
            }
        }
        draw->AddText({p.x + 16 * dpi, p.y + size.y - 28 * dpi}, IM_COL32(214, 220, 230, 230),
                      flying_ ? "WASD move  |  Q/E rise  |  Shift boost"
                              : "RMB + WASD fly  |  Wheel speed  |  F focus  |  Click select");
        if (playing_ && !paused_)
            for (std::size_t i = 0; i < snapshot_count_; ++i) {
                auto a = scene.actor(snapshot_handles_[i]);
                if (a && (a->kind == ActorKind::cube || a->kind == ActorKind::sphere)) {
                    a->transform.rotation.z += io.DeltaTime * 20;
                    auto done = scene.update(snapshot_handles_[i], *a);
                    if (!done)
                        log(done.error().message);
                }
            }
        if (playing_) {
            draw->AddRect(p, add(p, size), IM_COL32(83, 197, 126, 255), 0, 0, 2 * dpi);
        }
    }
    ImGui::End();
    outliner(scene);
    details(scene);
    content(scene);
    if (show_tools_) {
        telemetry();
        graph();
        console(scene);
    }
    if (focus_content_ && show_content_ && ImGui::GetFrameCount() > 1) {
        ImGui::SetWindowFocus("Content Browser");
        focus_content_ = false;
    }
}
void Workspace::telemetry() noexcept {
    if (ImGui::Begin("Telemetry")) {
        const double budget = budget_ == 0 ? 1000.0 / 60 : 1000.0 / 144;
        const float dpi = ImGui::GetFontSize() / 15;
        ImGui::SetNextItemWidth(96 * dpi);
        ImGui::Combo("Target", &budget_,
                     "60 Hz\0"
                     "144 Hz\0");
        ImGui::SameLine();
        ImGui::Text("Frame %.2f ms / %.1f FPS", last_.frame_ms,
                    last_.frame_ms > 0 ? 1000 / last_.frame_ms : 0);
        char utilization[96]{};
        std::snprintf(utilization, sizeof(utilization), "%.2f / %.2f ms (%s)", last_.frame_ms, budget,
                      last_.frame_ms <= budget ? "within budget" : "over budget");
        ImGui::ProgressBar(static_cast<float>(std::clamp(last_.frame_ms / budget, 0.0, 1.0)), {-1, 0},
                           utilization);
        if (ImGui::BeginTable("measurements", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("GPU and memory", ImGuiTableColumnFlags_WidthStretch, 0.35F);
            ImGui::TableSetupColumn("CPU trace", ImGuiTableColumnFlags_WidthStretch, 0.65F);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("GPU / main frame");
            if (last_.gpu.gpu_timing_valid)
                ImGui::Text("%.0f us", last_.gpu.gpu_us);
            else
                ImGui::TextDisabled("Awaiting completion");
            ImGui::Spacing();
            ImGui::TextUnformatted("VRAM / local budget");
            if (last_.gpu.budget_valid && last_.gpu.local_budget) {
                ImGui::Text("%.0f / %.0f MiB", static_cast<double>(last_.gpu.local_usage) / 1048576,
                            static_cast<double>(last_.gpu.local_budget) / 1048576);
                const auto fraction = static_cast<float>(static_cast<double>(last_.gpu.local_usage) /
                                                         static_cast<double>(last_.gpu.local_budget));
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                      fraction > 0.85F ? rgb(242, 190, 97) : rgb(108, 92, 231));
                ImGui::ProgressBar(std::clamp(fraction, 0.0F, 1.0F), {-1, 0}, "Utilization");
                ImGui::PopStyleColor();
            } else
                ImGui::TextWrapped("Budget unavailable on this driver");
            ImGui::Text("UI draws: %u", last_.draws);
            ImGui::TableNextColumn();
            if (ImPlot::BeginPlot("##frame_history", {-1, 80 * dpi},
                                  ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {
                ImPlot::SetupAxes("Sample index", "ms", ImPlotAxisFlags_NoTickLabels,
                                  ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxisLimits(ImAxis_X1, 0, 240, ImGuiCond_Always);
                const auto offset = count_ == cpu_ms_.size() ? static_cast<int>(cursor_) : 0;
                ImPlot::SetNextLineStyle({160.0F / 255, 146.0F / 255, 1, 1});
                ImPlot::PlotLine("Frame", cpu_ms_.data(), static_cast<int>(count_), 1, 0,
                                 ImPlotLineFlags_None, offset);
                if (last_.gpu.gpu_timing_valid) {
                    ImPlot::SetNextLineStyle({0.4F, 0.82F, 0.73F, 1});
                    ImPlot::PlotLine("GPU", gpu_ms_.data(), static_cast<int>(count_), 1, 0,
                                     ImPlotLineFlags_None, offset);
                }
                ImPlot::EndPlot();
            }
            ImGui::TextUnformatted("CPU elapsed (includes waits, us)");
            const char *labels[]{"Events / resize", "Workspace", "Acquire / record", "Present"};
            if (ImGui::BeginTable("cpu_times", 2, ImGuiTableFlags_SizingStretchSame)) {
                for (std::size_t i = 0; i < last_.cpu_us.size(); ++i) {
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", labels[i]);
                    ImGui::Text("%.0f us", last_.cpu_us[i]);
                }
                ImGui::EndTable();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}
void Workspace::graph() noexcept {
    if (ImGui::Begin("Render graph")) {
        ImGui::TextDisabled("Drag nodes / middle-drag pan / wheel zoom");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            nodes_ = {{{24, 48}, {304, 48}, {584, 48}}};
            fit_graph_ = true;
        }
        ImGui::BeginChild("graph_canvas", {0, 0}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        auto origin = ImGui::GetCursorScreenPos();
        auto area = ImGui::GetContentRegionAvail();
        const auto &io = ImGui::GetIO();
        auto *draw = ImGui::GetWindowDrawList();
        const float dpi = ImGui::GetFontSize() / 15;
        // DockBuilder's first-frame sizes settle at the next NewFrame.
        if (fit_graph_ && ImGui::GetFrameCount() > 1 && area.x > 0 && area.y > 0) {
            zoom_ = std::clamp(std::min(area.x / (856 * dpi), area.y / (176 * dpi)), 0.25F, 1.0F);
            pan_ = {16 * dpi, 16 * dpi};
            fit_graph_ = false;
        }
        draw->PushClipRect(origin, add(origin, area), true);
        const bool hovered = ImGui::IsWindowHovered();
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            pan_ = add(pan_, io.MouseDelta);
        if (hovered && io.MouseWheel != 0) {
            const float next = std::clamp(zoom_ * std::pow(1.1F, io.MouseWheel), 0.4F, 2.0F);
            const ImVec2 mouse{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
            pan_ = {mouse.x - (mouse.x - pan_.x) * next / zoom_, mouse.y - (mouse.y - pan_.y) * next / zoom_};
            zoom_ = next;
        }
        const float canvas_scale = zoom_ * dpi;
        const float step = 32 * canvas_scale;
        for (float x = std::fmod(pan_.x, step); x < area.x; x += step)
            draw->AddLine(add(origin, {x, 0}), add(origin, {x, area.y}), IM_COL32(48, 48, 59, 255));
        for (float y = std::fmod(pan_.y, step); y < area.y; y += step)
            draw->AddLine(add(origin, {0, y}), add(origin, {area.x, y}), IM_COL32(48, 48, 59, 255));
        std::array<ImVec2, 3> positions{};
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            positions[i] = add(add(origin, pan_), mul(nodes_[i], canvas_scale));
        for (std::size_t i = 0; i < 2; ++i) {
            const auto a = add(positions[i], {224 * canvas_scale, 48 * canvas_scale});
            const auto b = add(positions[i + 1], {0, 48 * canvas_scale});
            draw->AddBezierCubic(a, add(a, {48 * canvas_scale, 0}), add(b, {-48 * canvas_scale, 0}), b,
                                 IM_COL32(160, 146, 255, 255), 2 * dpi);
        }
        const char *names[]{"Grid + geometry", "Viewport sample", "Editor composite"};
        const char *info[]{"Color attachment / write", "RGBA8 / fragment read", "Swapchain / present"};
        for (int i = 0; i < 3; ++i) {
            auto p = positions[static_cast<std::size_t>(i)];
            ImGui::SetCursorScreenPos(p);
            ImGui::PushID(i);
            ImGui::InvisibleButton("node", {224 * canvas_scale, 96 * canvas_scale});
            if (ImGui::IsItemClicked())
                selected_node_ = i;
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
                nodes_[static_cast<std::size_t>(i)] =
                    add(nodes_[static_cast<std::size_t>(i)], mul(io.MouseDelta, 1 / canvas_scale));
            draw->AddRectFilled(p, add(p, {224 * canvas_scale, 96 * canvas_scale}), IM_COL32(43, 43, 54, 255),
                                4 * dpi);
            draw->AddRect(p, add(p, {224 * canvas_scale, 96 * canvas_scale}),
                          i == selected_node_ ? IM_COL32(160, 146, 255, 255) : IM_COL32(77, 77, 94, 255),
                          4 * dpi);
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * zoom_,
                          add(p, {16 * canvas_scale, 16 * canvas_scale}), IM_COL32(240, 240, 245, 255),
                          names[i]);
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * zoom_,
                          add(p, {16 * canvas_scale, 56 * canvas_scale}), IM_COL32(185, 187, 200, 255),
                          info[i]);
            draw->AddCircleFilled(add(p, {0, 48 * canvas_scale}), 4 * canvas_scale,
                                  IM_COL32(160, 146, 255, 255));
            draw->AddCircleFilled(add(p, {224 * canvas_scale, 48 * canvas_scale}), 4 * canvas_scale,
                                  IM_COL32(160, 146, 255, 255));
            ImGui::PopID();
        }
        draw->PopClipRect();
        ImGui::EndChild();
    }
    ImGui::End();
}
} // namespace souls::editor
