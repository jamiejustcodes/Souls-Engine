#include "editor/Workspace.hpp"
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
void Workspace::spawn(Scene &scene, ActorKind kind) noexcept {
    if (scene.size() >= 256) {
        log("Editor actor limit: 256");
        return;
    }
    char label[64]{};
    std::snprintf(label, sizeof(label), "%s_%zu", kind_name(kind), scene.size());
    auto h = scene.spawn(kind, label, {0, 0, 1});
    if (h) {
        selected_ = *h;
        log("Actor added to DefaultMap");
    } else
        log(h.error().message);
}
void Workspace::duplicate_actor(Scene &scene, EntityHandle entity) noexcept {
    if (scene.size() >= 256) {
        log("Editor actor limit: 256");
        return;
    }
    auto h = scene.duplicate(entity);
    if (h)
        selected_ = *h;
    else
        log(h.error().message);
}
void Workspace::play(Scene &scene) noexcept {
    if (scene.size() > snapshot_.size()) {
        log("Simulation supports up to 256 actors");
        return;
    }
    snapshot_count_ = 0;
    for (auto h : scene.actors()) {
        auto a = scene.actor(h);
        snapshot_[snapshot_count_] = *a;
        snapshot_handles_[snapshot_count_++] = h;
    }
    playing_ = true;
    paused_ = false;
    ejected_ = false;
    log("Simulation started: primitives rotate; Stop restores the edit world");
}
void Workspace::stop(Scene &scene) noexcept {
    std::array<EntityHandle, 256> remove{};
    std::size_t count = 0;
    for (auto h : scene.actors()) {
        bool original = false;
        for (std::size_t i = 0; i < snapshot_count_; ++i)
            if (snapshot_handles_[i] == h)
                original = true;
        if (!original && count < remove.size())
            remove[count++] = h;
    }
    for (std::size_t i = 0; i < count; ++i) {
        auto done = scene.remove(remove[i]);
        if (!done)
            log(done.error().message);
    }
    for (std::size_t i = 0; i < snapshot_count_; ++i) {
        auto done = scene.update(snapshot_handles_[i], snapshot_[i]);
        if (!done) {
            auto h = scene.spawn(snapshot_[i].kind, snapshot_[i].label.data(), snapshot_[i].transform);
            if (h) {
                auto restored = scene.update(*h, snapshot_[i]);
                if (!restored)
                    log(restored.error().message);
            } else
                log(h.error().message);
        }
    }
    selected_ = {};
    playing_ = paused_ = ejected_ = false;
    log("Simulation stopped; edit-world actor state restored");
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
    selected_ = cube;
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
    auto copy = scene.duplicate(cube);
    if (!copy || !scene.remove(*copy) || scene.actor(*copy))
        return fail();
    selected_ = cube;
    log("Workflow verified: selection, edits, duplication, deletion, simulation restore");
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
        if (ImGui::TreeNodeEx("World / DefaultMap", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int group = 0; group < 2; ++group) {
                if (ImGui::TreeNodeEx(group == 0 ? "Lighting" : "Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (auto h : scene.actors()) {
                        auto a = scene.actor(h);
                        bool light = a->kind == ActorKind::light || a->kind == ActorKind::sky;
                        if ((group == 0) != light || !matches(a->label.data(), actor_search_.data()))
                            continue;
                        ImGui::PushID(static_cast<int>(h.index));
                        const float dpi = ImGui::GetFontSize() / 15;
                        const auto eye = ImGui::GetCursorScreenPos();
                        if (ImGui::InvisibleButton("visibility", {24 * dpi, 18 * dpi})) {
                            a->visible = !a->visible;
                            auto done = scene.update(h, *a);
                            if (!done)
                                log(done.error().message);
                        }
                        auto *eye_draw = ImGui::GetWindowDrawList();
                        const ImU32 tint =
                            a->visible ? IM_COL32(197, 204, 218, 255) : IM_COL32(105, 110, 124, 255);
                        const ImVec2 center{eye.x + 12 * dpi, eye.y + 9 * dpi};
                        eye_draw->AddBezierCubic({center.x - 8 * dpi, center.y},
                                                 {center.x - 3 * dpi, center.y - 7 * dpi},
                                                 {center.x + 3 * dpi, center.y - 7 * dpi},
                                                 {center.x + 8 * dpi, center.y}, tint, 1 * dpi);
                        eye_draw->AddBezierCubic({center.x - 8 * dpi, center.y},
                                                 {center.x - 3 * dpi, center.y + 7 * dpi},
                                                 {center.x + 3 * dpi, center.y + 7 * dpi},
                                                 {center.x + 8 * dpi, center.y}, tint, 1 * dpi);
                        eye_draw->AddCircleFilled(center, 2 * dpi, tint);
                        if (!a->visible)
                            eye_draw->AddLine({center.x - 8 * dpi, center.y + 7 * dpi},
                                              {center.x + 8 * dpi, center.y - 7 * dpi}, tint, 1 * dpi);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(a->visible ? "Hide actor" : "Show actor");
                        ImGui::SameLine();
                        if (ImGui::Selectable(a->label.data(), selected_ == h))
                            selected_ = h;
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                            selected_ = h;
                            camera_.focus({a->transform.x, a->transform.y, a->transform.z});
                        }
                        if (ImGui::BeginPopupContextItem()) {
                            selected_ = h;
                            if (ImGui::MenuItem("Duplicate"))
                                duplicate = h;
                            if (ImGui::MenuItem("Delete"))
                                erase = h;
                            if (ImGui::MenuItem("Rename")) {
                                show_details_ = true;
                                ImGui::SetWindowFocus("Details");
                                rename_ = true;
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::TextDisabled("%zu actors  |  %s", scene.size(), selected_ ? "1 selected" : "No selection");
    }
    ImGui::End();
    if (duplicate) {
        duplicate_actor(scene, duplicate);
    }
    if (erase) {
        auto done = scene.remove(erase);
        if (done)
            selected_ = {};
        else
            log(done.error().message);
    }
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
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                Vec3 p{a.transform.x, a.transform.y, a.transform.z};
                if (vector_input("Location", p, 0, 10, translate_snap_)) {
                    a.transform.x = p.x;
                    a.transform.y = p.y;
                    a.transform.z = p.z;
                    changed = true;
                }
                changed |= vector_input("Rotation", a.transform.rotation, 0, 15, rotate_snap_);
                changed |= vector_input("Scale", a.transform.scale, 1, 0.25F, scale_snap_);
                for (auto *v : {&a.transform.scale.x, &a.transform.scale.y, &a.transform.scale.z})
                    if (std::abs(*v) < 0.001F)
                        *v = 0.001F;
            }
            if (a.kind == ActorKind::cube || a.kind == ActorKind::sphere) {
                if (ImGui::CollapsingHeader("Mesh / Render", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("Static Mesh");
                    int mesh = a.kind == ActorKind::cube ? 0 : 1;
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##mesh", &mesh, "/Engine/Meshes/SM_Cube\0/Engine/Meshes/SM_Sphere\0")) {
                        a.kind = mesh == 0 ? ActorKind::cube : ActorKind::sphere;
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
            if (changed) {
                auto done = scene.update(selected_, a);
                if (!done)
                    log(done.error().message);
            }
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
        ImGui::TextDisabled("Built-in assets  |  Double-click mesh to place");
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
            const char *names[]{"SM_Cube",   "SM_Sphere",       "MI_Slate",
                                "MI_Copper", "Playground.hlsl", "Procedural Grid"};
            const char *tags[]{"StaticMesh", "StaticMesh", "Material", "Material", "Shader", "Procedural"};
            ImGui::BeginChild("asset_grid", {0, 0}, ImGuiChildFlags_None);
            float width = 112 * dpi;
            int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (width + 8 * dpi)));
            int item = 0;
            for (int i = 0; i < 6; ++i) {
                if (!matches(names[i], asset_search_.data()) || (folder_ == 0 && i >= 4) ||
                    (folder_ == 1 && i != 4) || (folder_ == 2 && i != 5))
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
                } else if (i < 4) {
                    draw->AddCircleFilled(c, 24 * dpi,
                                          i == 1 || i == 3 ? IM_COL32(176, 113, 68, 255)
                                                           : IM_COL32(113, 136, 167, 255));
                    draw->AddCircleFilled({c.x - 7 * dpi, c.y - 8 * dpi}, 8 * dpi,
                                          IM_COL32(207, 192, 173, 100));
                } else {
                    draw->AddText({c.x - 20 * dpi, c.y - 8 * dpi}, IM_COL32(169, 156, 255, 255),
                                  i == 4 ? "</>" : "# #");
                }
                draw->AddText({p.x + 8 * dpi, p.y + 69 * dpi}, IM_COL32(237, 239, 245, 255), names[i]);
                draw->AddText({p.x + 8 * dpi, p.y + 87 * dpi}, IM_COL32(166, 158, 203, 255), tags[i]);
                if (ImGui::IsItemClicked())
                    asset_ = i;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && i < 2)
                    spawn(scene, i == 0 ? ActorKind::cube : ActorKind::sphere);
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
