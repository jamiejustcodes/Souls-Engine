#include "editor/Workspace.hpp"
#include <ImGuizmo.h>
#include <SDL3/SDL.h>
#include <cctype>
#include <cstdio>
#include <cstring>
namespace souls::editor {
namespace {
bool matches(const char *text, const char *search) noexcept {
    if (!*search)
        return true;
    for (; *text; ++text) {
        auto a = text, b = search;
        while (*a && *b &&
               std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) {
            ++a;
            ++b;
        }
        if (!*b)
            return true;
    }
    return false;
}
const char *kind_name(ActorKind k) noexcept {
    switch (k) {
    case ActorKind::cube:
        return "Cube";
    case ActorKind::sphere:
        return "Sphere";
    case ActorKind::cylinder:
        return "Cylinder";
    case ActorKind::wedge:
        return "Wedge";
    case ActorKind::capsule:
        return "Capsule";
    case ActorKind::plane:
        return "Plane";
    case ActorKind::light:
        return "Directional Light";
    case ActorKind::sky:
        return "Sky Atmosphere";
    default:
        return "Procedural Grid";
    }
}
bool vector_input(const char *label, Vec3 &v, float reset, float step, bool snap) noexcept {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        v = {reset, reset, reset};
        changed = true;
    }
    const ImVec4 colors[]{{0.65F, 0.18F, 0.19F, 1}, {0.16F, 0.46F, 0.26F, 1}, {0.16F, 0.32F, 0.65F, 1}};
    float *fields[]{&v.x, &v.y, &v.z};
    const char *axes[]{"X", "Y", "Z"};
    if (ImGui::BeginTable("xyz", 3, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < 3; ++i) {
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Button, colors[i]);
            if (ImGui::Button(axes[i])) {
                *fields[i] = reset;
                changed = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 2);
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::DragFloat("##value", fields[i], label[0] == 'S' ? 0.01F : 0.1F, 0, 0, "%.2f");
            if (snap && ImGui::IsItemDeactivatedAfterEdit()) {
                *fields[i] = std::round(*fields[i] / step) * step;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
    return changed;
}
} // namespace
void Workspace::event(const SDL_Event &e) noexcept {
    if (e.type == SDL_EVENT_MOUSE_MOTION && flying_) {
        mouse_x_ += e.motion.xrel;
        mouse_y_ += e.motion.yrel;
    }
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        if (gizmo_edit_)
            finish_edit(true);
        marquee_ = false;
        flying_ = false;
        if (auto *w = SDL_GetWindowFromID(e.window.windowID))
            SDL_SetWindowRelativeMouseMode(w, false);
    }
}
void Workspace::log(const char *text) noexcept {
    std::snprintf(log_[log_cursor_].data(), log_[log_cursor_].size(), "%s", text);
    log_cursor_ = (log_cursor_ + 1) % 64;
    log_count_ = std::min(log_count_ + 1, 64U);
}
void Workspace::finish_edit(bool cancel) noexcept {
    if (document_.editing()) {
        auto done = cancel ? document_.cancel_edit() : document_.commit_edit();
        if (!done)
            log(done.error().message);
    }
    if (gizmo_edit_)
        ImGuizmo::Enable(false);
    gizmo_edit_ = details_edit_ = false;
    edit_count_ = 0;
    selected_ = document_.primary();
}
void Workspace::select_entity(EntityHandle entity, SelectionMode mode) noexcept {
    finish_edit();
    document_.select(entity, mode);
    selected_ = document_.primary();
}
void Workspace::undo(bool redo) noexcept {
    finish_edit();
    auto done = redo ? document_.redo() : document_.undo();
    if (!done)
        log(done.error().message);
    selected_ = document_.primary();
}
void Workspace::erase_selection() noexcept {
    finish_edit();
    auto done = document_.erase_selection();
    if (!done)
        log(done.error().message);
    selected_ = document_.primary();
}
bool Workspace::selection_locked() const noexcept {
    for (auto h : document_.selection()) {
        auto a = scene_.actor(h);
        if (a && a->locked)
            return true;
    }
    return false;
}
Vec3 Workspace::selection_center() const noexcept {
    Vec3 center{};
    std::size_t count = 0;
    for (auto h : document_.selection()) {
        if (auto a = scene_.actor(h); a) {
            center = center + Vec3{a->transform.x, a->transform.y, a->transform.z};
            ++count;
        }
    }
    return count ? center * (1 / static_cast<float>(count)) : center;
}
void Workspace::begin_transform_edit(const char *label) noexcept {
    if (document_.editing())
        return;
    auto done = document_.begin_edit(label);
    if (!done) {
        log(done.error().message);
        return;
    }
    edit_count_ = 0;
    for (auto h : document_.selection()) {
        auto a = scene_.actor(h);
        if (a && edit_count_ < edit_actors_.size()) {
            edit_handles_[edit_count_] = h;
            edit_actors_[edit_count_++] = *a;
        }
    }
    if (auto a = scene_.actor(selected_); a)
        edit_primary_ = *a;
}
void Workspace::preview_details(const Actor &actor) noexcept {
    begin_transform_edit("Edit actor properties");
    if (!document_.editing())
        return;
    details_edit_ = true;
    // Numeric multi-edit applies relative translation/rotation/scale from the gesture start.
    for (std::size_t i = 0; i < edit_count_; ++i) {
        auto value = edit_actors_[i];
        if (edit_handles_[i] == selected_)
            value = actor;
        else {
            const auto &from = edit_primary_.transform;
            const auto &to = actor.transform;
            value.transform.x += to.x - from.x;
            value.transform.y += to.y - from.y;
            value.transform.z += to.z - from.z;
            value.transform.rotation = value.transform.rotation + to.rotation - from.rotation;
            value.transform.scale = {value.transform.scale.x * to.scale.x / from.scale.x,
                                     value.transform.scale.y * to.scale.y / from.scale.y,
                                     value.transform.scale.z * to.scale.z / from.scale.z};
            if (actor.visible != edit_primary_.visible)
                value.visible = actor.visible;
            if (actor.locked != edit_primary_.locked)
                value.locked = actor.locked;
            if (is_primitive(value.kind) && is_primitive(actor.kind)) {
                if (actor.kind != edit_primary_.kind)
                    value.kind = actor.kind;
                if (actor.material != edit_primary_.material)
                    value.material = actor.material;
                if (actor.color.x != edit_primary_.color.x || actor.color.y != edit_primary_.color.y ||
                    actor.color.z != edit_primary_.color.z)
                    value.color = actor.color;
            }
        }
        auto done = document_.preview(edit_handles_[i], value);
        if (!done) {
            log(done.error().message);
            finish_edit(true);
            return;
        }
    }
}
void Workspace::spawn(Scene &scene, ActorKind kind) noexcept {
    finish_edit();
    char label[64]{};
    std::snprintf(label, sizeof(label), "%s_%zu", kind_name(kind), scene.size());
    auto h = document_.spawn(kind, label, {0, 0, primitive_bounds(kind).z});
    if (h) {
        selected_ = document_.primary();
        log("Actor added");
    } else
        log(h.error().message);
}
void Workspace::duplicate_actor(Scene &, EntityHandle entity) noexcept {
    finish_edit();
    if (std::find(document_.selection().begin(), document_.selection().end(), entity) ==
        document_.selection().end())
        document_.select(entity);
    auto done = document_.duplicate_selection();
    if (!done)
        log(done.error().message);
    selected_ = document_.primary();
}
void Workspace::play(Scene &) noexcept {
    finish_edit();
    auto done = document_.begin_play();
    if (!done) {
        log(done.error().message);
        return;
    }
    playing_ = true;
    paused_ = ejected_ = false;
    log("Simulation started; Stop restores the edit world");
}
void Workspace::stop(Scene &) noexcept {
    finish_edit();
    auto done = document_.stop_play();
    if (!done) {
        log(done.error().message);
        return;
    }
    selected_ = document_.primary();
    playing_ = paused_ = ejected_ = false;
    log("Simulation stopped; edit world restored");
}
Result<void> Workspace::verify_workflow(Scene &scene) noexcept {
    auto fail = []() -> Result<void> {
        return std::unexpected(Error{ErrorCode::invalid_argument, "Editor workflow regression"});
    };
    EntityHandle cube{};
    for (auto h : scene.actors()) {
        auto a = scene.actor(h);
        if (a->kind == ActorKind::cube)
            cube = h;
    }
    if (!cube)
        return fail();
    auto original = scene.actor(cube);
    select_entity(cube);
    play(scene);
    if (!playing_ || paused_)
        return fail();
    auto changed = *original;
    changed.transform.x = 9;
    changed.transform.rotation.z = 42;
    changed.visible = false;
    if (!scene.update(cube, changed))
        return fail();
    spawn(scene, ActorKind::sphere);
    auto temporary = selected_;
    if (!temporary || scene.size() != 6)
        return fail();
    paused_ = true;
    ejected_ = true;
    stop(scene);
    auto restored = scene.actor(cube);
    if (playing_ || paused_ || ejected_ || scene.size() != 5 || scene.actor(temporary) ||
        restored->transform.x != original->transform.x ||
        restored->transform.rotation.z != original->transform.rotation.z ||
        restored->visible != original->visible)
        return fail();
    select_entity(cube);
    if (!document_.duplicate_selection() || scene.size() != 6 || !document_.undo() || scene.size() != 5 ||
        !document_.redo() || scene.size() != 6 || !document_.undo())
        return fail();
    select_entity(cube);
    auto edited = *scene.actor(cube);
    edited.transform.x += 2;
    preview_details(edited);
    edited.transform.x += 3;
    preview_details(edited);
    finish_edit();
    if (!document_.undo() || scene.actor(cube)->transform.x != original->transform.x)
        return fail();
    if (!document_.redo() || scene.actor(cube)->transform.x != edited.transform.x || !document_.undo())
        return fail();
    // Smoke rendering covers every uploaded mesh, not only the two default actors.
    constexpr ActorKind parts[]{ActorKind::cylinder, ActorKind::wedge, ActorKind::capsule, ActorKind::plane};
    for (std::size_t i = 0; i < 4; ++i) {
        auto added = document_.spawn(parts[i], kind_name(parts[i]),
                                     {-3 + static_cast<float>(i) * 2, 4, primitive_bounds(parts[i]).z});
        if (!added)
            return std::unexpected(added.error());
    }
    select_entity(cube);
    log("Workflow verified: coalesced edits, undo/redo, simulation restore, all six meshes");
    return {};
}
void Workspace::outliner(Scene &scene) noexcept {
    if (!show_outliner_)
        return;
    EntityHandle duplicate{}, erase{};
    if (ImGui::Begin("World Outliner", &show_outliner_)) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##actor_search", "Search Actors...", actor_search_.data(),
                                 actor_search_.size());
        auto row = [&](EntityHandle h) {
            auto a = scene.actor(h);
            if (!a || !matches(a->label.data(), actor_search_.data()))
                return;
            ImGui::PushID(static_cast<int>(h.index));
            const float dpi = ImGui::GetFontSize() / 15;
            auto eye = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("visibility", {24 * dpi, 18 * dpi})) {
                finish_edit();
                auto done = document_.begin_edit("Toggle actor visibility");
                a->visible = !a->visible;
                if (done)
                    done = document_.preview(h, *a);
                if (done)
                    done = document_.commit_edit();
                else
                    finish_edit(true);
                if (!done)
                    log(done.error().message);
            }
            auto *draw = ImGui::GetWindowDrawList();
            const auto tint = a->visible ? IM_COL32(197, 204, 218, 255) : IM_COL32(105, 110, 124, 255);
            ImVec2 c{eye.x + 12 * dpi, eye.y + 9 * dpi};
            draw->AddBezierCubic({c.x - 8 * dpi, c.y}, {c.x - 3 * dpi, c.y - 7 * dpi},
                                 {c.x + 3 * dpi, c.y - 7 * dpi}, {c.x + 8 * dpi, c.y}, tint, dpi);
            draw->AddBezierCubic({c.x - 8 * dpi, c.y}, {c.x - 3 * dpi, c.y + 7 * dpi},
                                 {c.x + 3 * dpi, c.y + 7 * dpi}, {c.x + 8 * dpi, c.y}, tint, dpi);
            draw->AddCircleFilled(c, 2 * dpi, tint);
            if (!a->visible)
                draw->AddLine({c.x - 8 * dpi, c.y + 7 * dpi}, {c.x + 8 * dpi, c.y - 7 * dpi}, tint, dpi);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(a->visible ? "Hide actor" : "Show actor");
            ImGui::SameLine();
            const bool selected = std::find(document_.selection().begin(), document_.selection().end(), h) !=
                                  document_.selection().end();
            char label[96]{};
            std::snprintf(label, sizeof(label), "%s%s", a->locked ? "[L] " : "", a->label.data());
            if (ImGui::Selectable(label, selected))
                select_entity(h, ImGui::GetIO().KeyCtrl    ? SelectionMode::toggle
                                 : ImGui::GetIO().KeyShift ? SelectionMode::add
                                                           : SelectionMode::replace);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                camera_.focus({a->transform.x, a->transform.y, a->transform.z});
            if (ImGui::BeginPopupContextItem()) {
                if (!selected)
                    select_entity(h);
                if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                    duplicate = h;
                if (ImGui::MenuItem("Delete", "Delete", false, !selection_locked()))
                    erase = h;
                if (ImGui::MenuItem("Rename")) {
                    show_details_ = true;
                    ImGui::SetWindowFocus("Details");
                    rename_ = true;
                }
                if (ImGui::MenuItem(a->locked ? "Unlock selection" : "Lock selection")) {
                    finish_edit();
                    auto done = document_.set_selected_locked(!a->locked);
                    if (!done)
                        log(done.error().message);
                }
                if (ImGui::MenuItem("Group selection", "Ctrl+G", false, !selection_locked()))
                    group_prompt_ = true;
                if (ImGui::MenuItem("Ungroup", nullptr, false, !selection_locked())) {
                    finish_edit();
                    auto done = document_.ungroup_selection();
                    if (!done)
                        log(done.error().message);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        };
        if (ImGui::TreeNodeEx("World / DefaultMap", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int category = 0; category < 2; ++category) {
                ImGui::PushID(category);
                if (ImGui::TreeNodeEx(category == 0 ? "Lighting" : "Geometry",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
                    std::array<std::array<char, 64>, EditorDocument::actor_limit> groups{};
                    std::size_t count = 0;
                    for (auto h : scene.actors()) {
                        auto a = scene.actor(h);
                        const bool light = a->kind == ActorKind::light || a->kind == ActorKind::sky;
                        if ((category == 0) != light)
                            continue;
                        if (!a->group[0])
                            row(h);
                        else {
                            bool found = false;
                            for (std::size_t i = 0; i < count; ++i)
                                found |= groups[i] == a->group;
                            if (!found && count < groups.size())
                                groups[count++] = a->group;
                        }
                    }
                    for (std::size_t i = 0; i < count; ++i) {
                        const bool open = ImGui::TreeNodeEx(groups[i].data(), ImGuiTreeNodeFlags_DefaultOpen);
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                            finish_edit();
                            document_.clear_selection();
                            for (auto h : scene.actors()) {
                                auto a = scene.actor(h);
                                if (a->group == groups[i])
                                    document_.select(h, SelectionMode::add);
                            }
                            selected_ = document_.primary();
                        }
                        if (open) {
                            for (auto h : scene.actors()) {
                                auto a = scene.actor(h);
                                const bool light = a->kind == ActorKind::light || a->kind == ActorKind::sky;
                                if ((category == 0) == light && a->group == groups[i])
                                    row(h);
                            }
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::TextDisabled("%zu actors  |  %zu selected", scene.size(), document_.selection().size());
    }
    ImGui::End();
    if (duplicate)
        duplicate_actor(scene, duplicate);
    if (erase)
        erase_selection();
}
void Workspace::details(Scene &scene) noexcept {
    if (!show_details_)
        return;
    if (ImGui::Begin("Details", &show_details_)) {
        ImGui::BeginDisabled(playing_ && !ejected_);
        auto actor = scene.actor(selected_);
        if (!actor) {
            ImGui::TextDisabled("Select an actor to inspect");
            ImGui::TextWrapped("Click a primitive in the viewport or choose an actor in the World Outliner.");
        } else {
            auto &a = *actor;
            bool changed = false;
            if (rename_) {
                ImGui::SetKeyboardFocusHere();
                rename_ = false;
            }
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::InputText("##label", a.label.data(), a.label.size());
            ImGui::TextDisabled("%s  |  ID %llu", kind_name(a.kind),
                                static_cast<unsigned long long>(selected_.id()));
            changed |= ImGui::Checkbox("Active / Visible", &a.visible);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Locked", &a.locked);
            if (document_.selection().size() > 1)
                ImGui::TextDisabled("Editing %zu actors", document_.selection().size());
            if (*a.group.data())
                ImGui::TextDisabled("Group: %s", a.group.data());
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BeginDisabled(selection_locked());
                Vec3 p{a.transform.x, a.transform.y, a.transform.z};
                if (vector_input("Location", p, 0, translate_step_, translate_snap_)) {
                    a.transform.x = p.x;
                    a.transform.y = p.y;
                    a.transform.z = p.z;
                    changed = true;
                }
                changed |= vector_input("Rotation", a.transform.rotation, 0, rotate_step_, rotate_snap_);
                changed |= vector_input("Scale", a.transform.scale, 1, scale_step_, scale_snap_);
                for (auto *v : {&a.transform.scale.x, &a.transform.scale.y, &a.transform.scale.z})
                    if (std::abs(*v) < 0.001F)
                        *v = 0.001F;
                ImGui::EndDisabled();
            }
            if (is_primitive(a.kind)) {
                if (ImGui::CollapsingHeader("Mesh / Render", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("Static Mesh");
                    int mesh = static_cast<int>(primitive_mesh_index(a.kind));
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##mesh", &mesh,
                                     "SM_Cube\0SM_Sphere\0SM_Cylinder\0SM_Wedge\0SM_Capsule\0SM_Plane\0")) {
                        constexpr ActorKind kinds[]{ActorKind::cube,  ActorKind::sphere,  ActorKind::cylinder,
                                                    ActorKind::wedge, ActorKind::capsule, ActorKind::plane};
                        a.kind = kinds[mesh];
                        changed = true;
                    }
                    ImGui::TextDisabled("Material Instance");
                    int material = static_cast<int>(std::min(a.material, 2U));
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##material", &material,
                                     "MI_Slate / Blinn-Phong\0MI_Copper / Blinn-Phong\0Custom Instance\0")) {
                        a.material = static_cast<std::uint32_t>(material);
                        if (material == 0)
                            a.color = {0.36F, 0.44F, 0.61F};
                        if (material == 1)
                            a.color = {0.72F, 0.36F, 0.12F};
                        changed = true;
                    }
                    if (ImGui::ColorEdit3("Base Color", &a.color.x)) {
                        a.material = 2;
                        changed = true;
                    }
                }
            }
            if (a.kind == ActorKind::light &&
                ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::DragFloat("Intensity (lux)", &a.intensity, 100, 0, 800000, "%.0f lux",
                                            ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::ColorEdit3("Light Color", &a.color.x);
                ImGui::BeginDisabled();
                ImGui::DragFloat("Attenuation radius", &a.attenuation);
                ImGui::EndDisabled();
                ImGui::TextWrapped(
                    "Directional lights have no distance attenuation. Rotation controls the sun direction.");
            }
            if (a.kind == ActorKind::floor) {
                ImGui::TextWrapped("Infinite procedural grid / analytic pixel filtering");
            }
            if (ImGui::Button("Focus actor", {-1, 0}))
                camera_.focus({a.transform.x, a.transform.y, a.transform.z});
            if (changed)
                preview_details(a);
            if (details_edit_ && !ImGui::IsAnyItemActive())
                finish_edit();
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}
void Workspace::content(Scene &scene) noexcept {
    if (!show_content_)
        return;
    if (ImGui::Begin("Content Browser", &show_content_)) {
        const float dpi = ImGui::GetFontSize() / 15;
        ImGui::SetNextItemWidth(240 * dpi);
        ImGui::InputTextWithHint("##asset_search", "Search assets...", asset_search_.data(),
                                 asset_search_.size());
        ImGui::SameLine();
        ImGui::TextDisabled("Built-in assets  |  Drag parts into the viewport");
        if (ImGui::BeginTable("content_columns", 2, ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 220 * dpi);
            ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            if (ImGui::TreeNodeEx("/Game", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Selectable("Content", folder_ == 0))
                    folder_ = 0;
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("/Engine", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Selectable("Shaders", folder_ == 1))
                    folder_ = 1;
                if (ImGui::Selectable("Textures", folder_ == 2))
                    folder_ = 2;
                ImGui::TreePop();
            }
            ImGui::TableNextColumn();
            const char *names[]{"SM_Cube",  "SM_Sphere", "SM_Cylinder", "SM_Wedge",        "SM_Capsule",
                                "SM_Plane", "MI_Slate",  "MI_Copper",   "Playground.hlsl", "Procedural Grid"};
            const char *tags[]{"StaticMesh", "StaticMesh", "StaticMesh", "StaticMesh", "StaticMesh",
                               "StaticMesh", "Material",   "Material",   "Shader",     "Procedural"};
            constexpr ActorKind kinds[]{ActorKind::cube,  ActorKind::sphere,  ActorKind::cylinder,
                                        ActorKind::wedge, ActorKind::capsule, ActorKind::plane};
            ImGui::BeginChild("asset_grid", {0, 0}, ImGuiChildFlags_None);
            float width = 112 * dpi;
            int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (width + 8 * dpi)));
            int item = 0;
            for (int i = 0; i < 10; ++i) {
                if (!matches(names[i], asset_search_.data()) || (folder_ == 0 && i >= 8) ||
                    (folder_ == 1 && i != 8) || (folder_ == 2 && i != 9))
                    continue;
                ImGui::PushID(i);
                ImGui::BeginGroup();
                auto p = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("asset", {width, 106 * dpi});
                auto *draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(p, {p.x + width, p.y + 106 * dpi},
                                    asset_ == i ? IM_COL32(67, 57, 110, 255) : IM_COL32(43, 43, 54, 255),
                                    4 * dpi);
                const ImVec2 c{p.x + width / 2, p.y + 33 * dpi};
                if (i == 0) {
                    draw->AddQuadFilled({c.x, c.y - 22 * dpi}, {c.x + 25 * dpi, c.y - 9 * dpi},
                                        {c.x, c.y + 5 * dpi}, {c.x - 25 * dpi, c.y - 9 * dpi},
                                        IM_COL32(147, 166, 194, 255));
                    draw->AddQuadFilled({c.x - 25 * dpi, c.y - 9 * dpi}, {c.x, c.y + 5 * dpi},
                                        {c.x, c.y + 29 * dpi}, {c.x - 25 * dpi, c.y + 15 * dpi},
                                        IM_COL32(75, 90, 117, 255));
                    draw->AddQuadFilled({c.x, c.y + 5 * dpi}, {c.x + 25 * dpi, c.y - 9 * dpi},
                                        {c.x + 25 * dpi, c.y + 15 * dpi}, {c.x, c.y + 29 * dpi},
                                        IM_COL32(102, 120, 149, 255));
                } else if (i == 1 || i == 6 || i == 7) {
                    draw->AddCircleFilled(c, 24 * dpi,
                                          i == 1 || i == 7 ? IM_COL32(176, 113, 68, 255)
                                                           : IM_COL32(113, 136, 167, 255));
                    draw->AddCircleFilled({c.x - 7 * dpi, c.y - 8 * dpi}, 8 * dpi,
                                          IM_COL32(207, 192, 173, 100));
                } else if (i == 2) {
                    draw->AddRectFilled({c.x - 19 * dpi, c.y - 15 * dpi}, {c.x + 19 * dpi, c.y + 21 * dpi},
                                        IM_COL32(93, 112, 145, 255));
                    draw->AddEllipseFilled({c.x, c.y + 21 * dpi}, {19 * dpi, 7 * dpi},
                                           IM_COL32(93, 112, 145, 255));
                    draw->AddEllipseFilled({c.x, c.y - 15 * dpi}, {19 * dpi, 7 * dpi},
                                           IM_COL32(160, 179, 207, 255));
                } else if (i == 3) {
                    draw->AddTriangleFilled({c.x - 25 * dpi, c.y + 20 * dpi},
                                            {c.x + 20 * dpi, c.y + 20 * dpi},
                                            {c.x + 20 * dpi, c.y - 22 * dpi}, IM_COL32(134, 153, 183, 255));
                    draw->AddTriangleFilled({c.x + 20 * dpi, c.y + 20 * dpi},
                                            {c.x + 30 * dpi, c.y + 10 * dpi},
                                            {c.x + 20 * dpi, c.y - 22 * dpi}, IM_COL32(80, 96, 126, 255));
                } else if (i == 4) {
                    draw->AddRectFilled({c.x - 14 * dpi, c.y - 10 * dpi}, {c.x + 14 * dpi, c.y + 10 * dpi},
                                        IM_COL32(113, 136, 167, 255));
                    draw->AddCircleFilled({c.x, c.y - 10 * dpi}, 14 * dpi, IM_COL32(113, 136, 167, 255));
                    draw->AddCircleFilled({c.x, c.y + 10 * dpi}, 14 * dpi, IM_COL32(113, 136, 167, 255));
                } else if (i == 5) {
                    draw->AddQuadFilled({c.x, c.y - 18 * dpi}, {c.x + 30 * dpi, c.y}, {c.x, c.y + 18 * dpi},
                                        {c.x - 30 * dpi, c.y}, IM_COL32(134, 153, 183, 255));
                } else {
                    draw->AddText({c.x - 20 * dpi, c.y - 8 * dpi}, IM_COL32(169, 156, 255, 255),
                                  i == 8   ? "</>"
                                  : i == 9 ? "# #"
                                  : i == 2 ? "CYL"
                                  : i == 3 ? " /|"
                                  : i == 4 ? "CAP"
                                           : "___");
                }
                draw->AddText({p.x + 8 * dpi, p.y + 69 * dpi}, IM_COL32(237, 239, 245, 255), names[i]);
                draw->AddText({p.x + 8 * dpi, p.y + 87 * dpi}, IM_COL32(166, 158, 203, 255), tags[i]);
                if (ImGui::IsItemClicked())
                    asset_ = i;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && i < 6)
                    spawn(scene, kinds[i]);
                if (i < 6 && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("SOULS_PART", &kinds[i], sizeof(ActorKind));
                    ImGui::Text("Place %s", names[i]);
                    ImGui::EndDragDropSource();
                }
                ImGui::EndGroup();
                ImGui::PopID();
                if (++item % columns)
                    ImGui::SameLine();
            }
            ImGui::EndChild();
            ImGui::EndTable();
        }
    }
    ImGui::End();
}
void Workspace::console(Scene &scene) noexcept {
    if (focus_log_) {
        ImGui::SetNextWindowFocus();
        focus_log_ = false;
    }
    if (ImGui::Begin("Output Log")) {
        if (ImGui::SmallButton("Clear")) {
            log_count_ = log_cursor_ = 0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Commands: help, clear, reset, cube, sphere");
        ImGui::BeginChild("log_lines", {0, -ImGui::GetFrameHeightWithSpacing()});
        for (std::uint32_t i = 0; i < log_count_; ++i)
            ImGui::TextUnformatted(log_[(log_cursor_ + 64 - log_count_ + i) % 64].data());
        ImGui::EndChild();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##console", "Enter console command...", command_.data(),
                                     command_.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            log(command_.data());
            if (std::strcmp(command_.data(), "clear") == 0) {
                log_count_ = log_cursor_ = 0;
            } else if (std::strcmp(command_.data(), "reset") == 0)
                camera_ = Camera{};
            else if (std::strcmp(command_.data(), "cube") == 0)
                spawn(scene, ActorKind::cube);
            else if (std::strcmp(command_.data(), "sphere") == 0)
                spawn(scene, ActorKind::sphere);
            else if (std::strcmp(command_.data(), "help") == 0)
                log("help | clear | reset (camera) | cube | sphere");
            else
                log("Unknown command; type help");
            command_[0] = 0;
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::End();
}
} // namespace souls::editor
