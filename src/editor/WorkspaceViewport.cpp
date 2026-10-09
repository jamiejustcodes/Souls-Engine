#include "editor/Workspace.hpp"
#include <ImGuizmo.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <imgui_internal.h>
namespace souls::editor {
namespace {
ImVec2 add(ImVec2 a, ImVec2 b) noexcept {
    return {a.x + b.x, a.y + b.y};
}
} // namespace
void Workspace::viewport(ImTextureID texture, rhi::Extent extent, Scene &scene, SDL_Window *window,
                         float pixel_density) noexcept {
    const float dpi = ImGui::GetFontSize() / 15;
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
        ImGui::Checkbox("T", &translate_snap_);
        ImGui::SameLine();
        ImGui::Checkbox("R", &rotate_snap_);
        ImGui::SameLine();
        ImGui::Checkbox("S", &scale_snap_);
        const char *tools[]{"Select [Q]", "Move [W]", "Rotate [E]", "Scale [R]"};
        for (int i = 0; i < 4; ++i) {
            if (i)
                ImGui::SameLine();
            const bool active = static_cast<int>(tool_) == i;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, {0.42F, 0.36F, 0.90F, 1});
            if (ImGui::Button(tools[i])) {
                finish_edit();
                tool_ = static_cast<TransformTool>(i);
            }
            if (active)
                ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        if (ImGui::Button(local_space_ ? "Local" : "World")) {
            finish_edit();
            local_space_ = !local_space_;
        }
        ImGui::SameLine();
        if (ImGui::Button("Snap settings"))
            ImGui::OpenPopup("snap_settings");
        if (ImGui::BeginPopup("snap_settings")) {
            ImGui::DragFloat("Translation", &translate_step_, 0.1F, 0.01F, 100, "%.2f",
                             ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Rotation", &rotate_step_, 1, 1, 90, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Scale", &scale_step_, 0.01F, 0.01F, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextWrapped(
                "Group scale adjusts spacing and each part's local scale; actor transforms remain TRS.");
            ImGui::EndPopup();
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
        const bool image_hovered = ImGui::IsItemHovered();
        const auto p = ImGui::GetItemRectMin();
        bool hovered = image_hovered;
        auto &io = ImGui::GetIO();
        if (!files_.pending() && !gizmo_edit_ && (!playing_ || ejected_) && hovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
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
        auto *draw = ImGui::GetWindowDrawList();
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        if (ImGui::BeginDragDropTarget()) {
            if (const auto *payload =
                    ImGui::AcceptDragDropPayload("SOULS_PART", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                if (payload->DataSize == static_cast<int>(sizeof(ActorKind))) {
                    const auto kind = *static_cast<const ActorKind *>(payload->Data);
                    const auto ray = camera_.ray((io.MousePos.x - p.x) / size.x * 2 - 1,
                                                 1 - (io.MousePos.y - p.y) / size.y * 2, aspect);
                    Transform floor{};
                    for (auto h : scene.actors()) {
                        auto actor = scene.actor(h);
                        if (actor && actor->kind == ActorKind::floor) {
                            floor = actor->transform;
                            break;
                        }
                    }
                    auto placement = place_on_plane(camera_.position, ray, floor, kind,
                                                    translate_snap_ ? translate_step_ : 0);
                    if (placement) {
                        const auto &t = *placement;
                        const Vec3 position{t.x, t.y, t.z};
                        const auto matrix = camera_.projection(aspect) * camera_.view() *
                                            model_matrix(position, t.rotation, t.scale);
                        const auto bounds = primitive_bounds(kind);
                        std::array<ImVec2, 8> corners{};
                        for (std::size_t i = 0; i < 8; ++i) {
                            const auto point = transform_point(matrix, {(i & 1) ? bounds.x : -bounds.x,
                                                                        (i & 2) ? bounds.y : -bounds.y,
                                                                        (i & 4) ? bounds.z : -bounds.z});
                            corners[i] = {p.x + (point.x + 1) * size.x / 2, p.y + (1 - point.y) * size.y / 2};
                        }
                        draw->PushClipRect(p, add(p, size), true);
                        for (std::size_t i = 0; i < 8; ++i)
                            for (auto bit : {1U, 2U, 4U})
                                if (!(i & bit))
                                    draw->AddLine(corners[i], corners[i | bit], IM_COL32(116, 227, 166, 255),
                                                  2 * dpi);
                        draw->PopClipRect();
                        if (payload->IsDelivery()) {
                            finish_edit();
                            char label[64]{};
                            std::snprintf(label, sizeof(label), "Part_%zu", scene.size());
                            auto added = document_.spawn(kind, label, t);
                            if (!added)
                                log(added.error().message);
                            selected_ = document_.primary();
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        gizmo(p, size, aspect);
        const bool editing_allowed = (!playing_ || ejected_) && !files_.pending();
        if (hovered && editing_allowed && !flying_) {
            camera_.speed = std::clamp(camera_.speed * std::pow(1.2F, io.MouseWheel), 1.0F, 8.0F);
            const bool on_axis = io.MousePos.x > p.x + size.x - 98 * dpi && io.MousePos.y < p.y + 108 * dpi;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !on_axis && !ImGuizmo::IsOver() &&
                !ImGuizmo::IsUsing() && !ImGui::GetDragDropPayload()) {
                const auto hit =
                    scene.pick(camera_, camera_.ray((io.MousePos.x - p.x) / size.x * 2 - 1,
                                                    1 - (io.MousePos.y - p.y) / size.y * 2, aspect));
                select_entity(hit, io.KeyCtrl    ? SelectionMode::toggle
                                   : io.KeyShift ? SelectionMode::add
                                                 : SelectionMode::replace);
                if (!hit) {
                    marquee_ = true;
                    marquee_start_ = io.MousePos;
                }
            }
            if (!io.WantTextInput && !io.KeyCtrl && !gizmo_edit_) {
                if (ImGui::IsKeyPressed(ImGuiKey_Q))
                    tool_ = TransformTool::select;
                if (ImGui::IsKeyPressed(ImGuiKey_W))
                    tool_ = TransformTool::move;
                if (ImGui::IsKeyPressed(ImGuiKey_E))
                    tool_ = TransformTool::rotate;
                if (ImGui::IsKeyPressed(ImGuiKey_R))
                    tool_ = TransformTool::scale;
                if (ImGui::IsKeyPressed(ImGuiKey_F) && selected_)
                    camera_.focus(selection_center());
            }
        }
        if (marquee_) {
            const ImVec2 lo{std::min(marquee_start_.x, io.MousePos.x),
                            std::min(marquee_start_.y, io.MousePos.y)};
            const ImVec2 hi{std::max(marquee_start_.x, io.MousePos.x),
                            std::max(marquee_start_.y, io.MousePos.y)};
            draw->PushClipRect(p, add(p, size), true);
            draw->AddRectFilled(lo, hi, IM_COL32(108, 92, 231, 45));
            draw->AddRect(lo, hi, IM_COL32(169, 156, 255, 255));
            draw->PopClipRect();
            if (!ImGui::IsMouseDown(0)) {
                marquee_ = false;
                if (hi.x - lo.x > 4 * dpi || hi.y - lo.y > 4 * dpi) {
                    const auto matrix = camera_.projection(aspect) * camera_.view();
                    for (auto h : scene.actors()) {
                        auto a = scene.actor(h);
                        if (!a || !a->visible || a->locked || !is_primitive(a->kind))
                            continue;
                        Vec3 pos{a->transform.x, a->transform.y, a->transform.z};
                        if (dot(pos - camera_.position, camera_.forward()) <= 0.1F)
                            continue;
                        auto ndc = transform_point(matrix, pos);
                        ImVec2 point{p.x + (ndc.x + 1) * size.x / 2, p.y + (1 - ndc.y) * size.y / 2};
                        if (point.x >= lo.x && point.x <= hi.x && point.y >= lo.y && point.y <= hi.y)
                            document_.select(h, SelectionMode::add);
                    }
                    selected_ = document_.primary();
                }
            }
        }
        for (auto h : document_.selection()) {
            auto a = scene.actor(h);
            if (!a || !a->visible || !is_primitive(a->kind))
                continue;
            const auto &t = a->transform;
            const auto matrix = camera_.projection(aspect) * camera_.view() *
                                model_matrix({t.x, t.y, t.z}, t.rotation, t.scale);
            const auto bounds = primitive_bounds(a->kind);
            std::array<ImVec2, 8> corners{};
            bool in_front = true;
            for (std::size_t i = 0; i < 8; ++i) {
                Vec3 local{(i & 1) ? bounds.x : -bounds.x, (i & 2) ? bounds.y : -bounds.y,
                           (i & 4) ? bounds.z : -bounds.z};
                const auto &m = matrix.m;
                if (m[3] * local.x + m[7] * local.y + m[11] * local.z + m[15] <= 0.1F)
                    in_front = false;
                auto ndc = transform_point(matrix, local);
                corners[i] = {p.x + (ndc.x + 1) * size.x / 2, p.y + (1 - ndc.y) * size.y / 2};
            }
            if (in_front) {
                draw->PushClipRect(p, add(p, size), true);
                for (std::size_t i = 0; i < 8; ++i)
                    for (auto bit : {1U, 2U, 4U})
                        if (!(i & bit))
                            draw->AddLine(corners[i], corners[i | bit], IM_COL32(255, 174, 63, 220), dpi);
                draw->PopClipRect();
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
        draw->AddText(
            {p.x + 16 * dpi, p.y + size.y - 28 * dpi}, IM_COL32(214, 220, 230, 230),
            flying_
                ? "WASD move  |  Q/E rise  |  Shift boost"
                : "RMB + WASD fly  |  F focus  |  Ctrl-click multi-select  |  Drag empty space to select");
        if (playing_ && !paused_) {
            for (auto h : scene.actors()) {
                auto a = scene.actor(h);
                if (a && !a->locked && is_primitive(a->kind)) {
                    a->transform.rotation.z += io.DeltaTime * 20;
                    auto done = scene.update(h, *a);
                    if (!done)
                        log(done.error().message);
                }
            }
        }
        if (playing_) {
            draw->AddRect(p, add(p, size), IM_COL32(83, 197, 126, 255), 0, 0, 2 * dpi);
        }
    }
    ImGui::End();
}
void Workspace::gizmo(const ImVec2 &origin, const ImVec2 &size, float aspect) noexcept {
    const bool enabled = selected_ && tool_ != TransformTool::select && !selection_locked() && !flying_ &&
                         !files_.pending() && (!playing_ || ejected_);
    ImGuizmo::Enable(enabled);
    if (!enabled) {
        if (gizmo_edit_)
            finish_edit(true);
        return;
    }
    auto primary = scene_.actor(selected_);
    if (!primary)
        return;
    const bool single = document_.selection().size() == 1;
    auto initial = gizmo_edit_ ? gizmo_current_
                               : model_matrix(selection_center(),
                                              local_space_ || single ? primary->transform.rotation : Vec3{},
                                              single ? primary->transform.scale : Vec3{1, 1, 1});
    auto matrix = initial;
    const auto view = camera_.view(), projection = camera_.projection(aspect);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    ImGuizmo::SetOrthographic(false);
    const auto operation = tool_ == TransformTool::move     ? ImGuizmo::TRANSLATE
                           : tool_ == TransformTool::rotate ? ImGuizmo::ROTATE
                                                            : ImGuizmo::SCALE;
    const float step = tool_ == TransformTool::move     ? translate_step_
                       : tool_ == TransformTool::rotate ? rotate_step_
                                                        : scale_step_;
    const float snap[]{step, step, step};
    const bool snapping = tool_ == TransformTool::move     ? translate_snap_
                          : tool_ == TransformTool::rotate ? rotate_snap_
                                                           : scale_snap_;
    const bool changed = ImGuizmo::Manipulate(view.m.data(), projection.m.data(), operation,
                                              local_space_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                                              matrix.m.data(), nullptr, snapping ? snap : nullptr);
    if (ImGuizmo::IsUsing() && !gizmo_edit_) {
        finish_edit();
        begin_transform_edit(tool_ == TransformTool::move     ? "Move selection"
                             : tool_ == TransformTool::rotate ? "Rotate selection"
                                                              : "Scale selection");
        if (!document_.editing())
            return;
        gizmo_start_ = initial;
        gizmo_edit_ = true;
    }
    if (gizmo_edit_ && changed) {
        gizmo_current_ = matrix;
        for (std::size_t i = 0; i < edit_count_; ++i) {
            auto actor = edit_actors_[i];
            auto transform = apply_gizmo_delta(actor.transform, gizmo_start_, matrix, tool_,
                                               single ? edit_primary_.transform.scale : Vec3{1, 1, 1});
            if (!transform) {
                log(transform.error().message);
                finish_edit(true);
                return;
            }
            actor.transform = *transform;
            auto done = document_.preview(edit_handles_[i], actor);
            if (!done) {
                log(done.error().message);
                finish_edit(true);
                return;
            }
        }
    } else if (gizmo_edit_)
        gizmo_current_ = matrix;
    if (gizmo_edit_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        finish_edit(true);
        ImGuizmo::Enable(false);
    } else if (gizmo_edit_ && !ImGuizmo::IsUsing())
        finish_edit();
}
} // namespace souls::editor
