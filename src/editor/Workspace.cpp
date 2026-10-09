// Keep workspace layout and presentation here; actor editing lives in WorkspaceActors.cpp.
#include "editor/Workspace.hpp"
#include <ImGuizmo.h>
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
    c[ImGuiCol_ButtonHovered] = rgb(73, 73, 88);
    c[ImGuiCol_ButtonActive] = rgb(108, 92, 231);
    c[ImGuiCol_Header] = rgb(43, 43, 54);
    c[ImGuiCol_HeaderHovered] = rgb(57, 57, 69);
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
    ImGuizmo::BeginFrame();
    file_actions();
    selected_ = document_.primary();
    if (demo_active_) {
        demo_view(texture, window, pixel_density);
        return;
    }
    ImGui::BeginDisabled(files_.pending() || unsaved_prompt_);
    if (!ImGui::GetIO().WantTextInput && !files_.pending() && !unsaved_prompt_ && !group_prompt_ &&
        !flying_ && !gizmo_edit_) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z))
            undo();
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) ||
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
            undo(true);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S))
            save_level();
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S))
            save_level(true);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O))
            request_action(Action::open_level);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N))
            request_action(Action::new_level);
        if (selected_ && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D))
            duplicate_actor(scene, selected_);
        if (selected_ && ImGui::Shortcut(ImGuiKey_Delete))
            erase_selection();
        if (selected_ && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_G))
            group_prompt_ = true;
    }
    if (ImGui::BeginMainMenuBar()) {
        draw_brand(18 * dpi);
        ImGui::SameLine();
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New level", "Ctrl+N"))
                request_action(Action::new_level);
            if (ImGui::MenuItem("Open level...", "Ctrl+O"))
                request_action(Action::open_level);
            if (ImGui::MenuItem("Save level", "Ctrl+S", false, !playing_))
                save_level();
            if (ImGui::MenuItem("Save level as...", "Ctrl+Shift+S", false, !playing_))
                save_level(true);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset camera"))
                camera_ = Camera{};
            if (ImGui::MenuItem("Exit"))
                request_exit();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, document_.can_undo()))
                undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, document_.can_redo()))
                undo(true);
            ImGui::Separator();
            ImGui::BeginDisabled(!selected_);
            if (ImGui::MenuItem("Duplicate actor", "Ctrl+D")) {
                duplicate_actor(scene, selected_);
            }
            if (ImGui::MenuItem("Delete actor", "Delete")) {
                erase_selection();
            }
            if (ImGui::MenuItem("Group selection", "Ctrl+G"))
                group_prompt_ = true;
            if (ImGui::MenuItem("Ungroup")) {
                finish_edit();
                auto done = document_.ungroup_selection();
                if (!done)
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
            if (ImGui::MenuItem("Play Souls Courtyard"))
                show_demo(true);
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
    if (ImGui::BeginViewportSideBar("Main toolbar", vp, ImGuiDir_Up, 84 * dpi,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const char *tabs[]{"Home", "Build", "Test", "Tools"};
        for (int i = 0; i < 4; ++i) {
            if (i)
                ImGui::SameLine();
            if (ribbon_ == i)
                ImGui::PushStyleColor(ImGuiCol_Button, rgb(66, 58, 112));
            const bool active = ribbon_ == i;
            if (ImGui::Button(tabs[i]))
                ribbon_ = i;
            if (active)
                ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", document_.path()[0] ? "Level" : "Untitled",
                            document_.dirty() ? " *" : "");
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 182 * dpi));
        ImGui::PushStyleColor(ImGuiCol_Button, {0.10F, 0.34F, 0.22F, 1});
        if (ImGui::Button("Play demo", {100 * dpi, 0}))
            show_demo(true);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("About"))
            ImGui::OpenPopup("About Souls Engine");
        if (ImGui::BeginPopup("About Souls Engine")) {
            ImGui::Image(logo_, {304 * dpi, 204 * dpi});
            ImGui::TextUnformatted("Souls Engine");
            ImGui::TextDisabled("C++23 / D3D12 / Vulkan 1.3");
            ImGui::TextWrapped("Build a scene, play the courtyard, and inspect the frame.");
            ImGui::EndPopup();
        }
        ImGui::Separator();
        if (ribbon_ == 1) {
            constexpr ActorKind kinds[]{ActorKind::cube,  ActorKind::sphere,  ActorKind::cylinder,
                                        ActorKind::wedge, ActorKind::capsule, ActorKind::plane};
            const char *names[]{"Cube", "Sphere", "Cylinder", "Wedge", "Capsule", "Plane"};
            for (int i = 0; i < 6; ++i) {
                if (i)
                    ImGui::SameLine();
                if (ImGui::Button(names[i]))
                    spawn(scene, kinds[i]);
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("SOULS_PART", &kinds[i], sizeof(ActorKind));
                    ImGui::Text("Place %s", names[i]);
                    ImGui::EndDragDropSource();
                }
            }
            ImGui::SameLine();
        } else if (ribbon_ == 3) {
            if (ImGui::Button("Telemetry / Graph / Log"))
                show_tools_ = true;
            ImGui::SameLine();
        } else {
            ImGui::BeginDisabled(playing_);
            if (ImGui::Button("Save"))
                save_level();
            ImGui::SameLine();
            ImGui::BeginDisabled(!document_.can_undo());
            if (ImGui::Button("Undo"))
                undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!document_.can_redo());
            if (ImGui::Button("Redo"))
                undo(true);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ribbon_ != 1) {
            ImGui::SetNextItemWidth(120 * dpi);
            ImGui::Combo("##mode", &mode_, "Selection\0Landscape\0Modeling\0");
            ImGui::SameLine();
            if (ImGui::Button("+ Add actor"))
                ImGui::OpenPopup("quick_add");
            if (ImGui::BeginPopup("quick_add")) {
                if (ImGui::MenuItem("Cube"))
                    spawn(scene, ActorKind::cube);
                if (ImGui::MenuItem("Sphere"))
                    spawn(scene, ActorKind::sphere);
                if (ImGui::MenuItem("Cylinder"))
                    spawn(scene, ActorKind::cylinder);
                if (ImGui::MenuItem("Wedge"))
                    spawn(scene, ActorKind::wedge);
                if (ImGui::MenuItem("Capsule"))
                    spawn(scene, ActorKind::capsule);
                if (ImGui::MenuItem("Plane"))
                    spawn(scene, ActorKind::plane);
                if (ImGui::MenuItem("Directional Light"))
                    spawn(scene, ActorKind::light);
                ImGui::EndPopup();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu selected", document_.selection().size());
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() * 0.50F));
        ImGui::PushStyleColor(ImGuiCol_Button, {0.10F, 0.37F, 0.22F, 1});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.14F, 0.48F, 0.29F, 1});
        ImGui::BeginDisabled(playing_);
        if (ImGui::Button("Simulate", {80 * dpi, 0}))
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
        const auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22F, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24F, nullptr, &center);
        const auto details_dock = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55F, nullptr, &right);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderDockWindow("World Outliner", right);
        ImGui::DockBuilderDockWindow("Details", details_dock);
        for (auto title : {"Content Browser", "Telemetry", "Render graph", "Output Log"})
            ImGui::DockBuilderDockWindow(title, bottom);
        ImGui::DockBuilderFinish(dock);
    }
    viewport(texture, extent, scene, window, pixel_density);
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
    ImGui::EndDisabled();
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
