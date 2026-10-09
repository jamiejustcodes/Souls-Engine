#include <cstdio>
#include <cstring>
#include <limits>
#include <souls/editor/EditorDocument.hpp>
using namespace souls;
using namespace souls::editor;
namespace {
int check(bool okay, int line) {
    if (!okay)
        std::fprintf(stderr, "Editor document contract failed at line %d\n", line);
    return okay ? 0 : line;
}
#define REQUIRE(expression)                                                                                  \
    do {                                                                                                     \
        if (int error = check(static_cast<bool>(expression), __LINE__))                                      \
            return error;                                                                                    \
    } while (false)
Result<void> change(EditorDocument &document, Scene &scene, EntityHandle h, float x,
                    const char *label = "Move actor") {
    auto a = scene.actor(h);
    if (!a)
        return std::unexpected(a.error());
    auto begun = document.begin_edit(label);
    if (!begun)
        return begun;
    a->transform.x = x;
    auto done = document.preview(h, *a);
    if (!done)
        return done;
    return document.commit_edit();
}
bool overwrite(const char *path, const char *text) {
#ifdef _WIN32
    FILE *file = nullptr;
    if (fopen_s(&file, path, "wb") != 0)
        return false;
#else
    FILE *file = std::fopen(path, "wb");
#endif
    if (!file)
        return false;
    const bool okay = std::fputs(text, file) >= 0;
    return std::fclose(file) == 0 && okay;
}
} // namespace
int main() {
    constexpr char level[] = "document-contract.souls";
    constexpr char autosave[] = "document-recovery.souls";
    constexpr char malformed[] = "document-malformed.souls";
    auto scene = Scene::create();
    REQUIRE(scene && scene->playground());
    auto document = EditorDocument::create(*scene);
    REQUIRE(document && !document->dirty());
    EntityHandle cube{}, sphere{};
    for (auto h : scene->actors()) {
        auto a = scene->actor(h);
        if (a->kind == ActorKind::cube)
            cube = h;
        if (a->kind == ActorKind::sphere)
            sphere = h;
    }
    REQUIRE(cube && sphere);
    const auto cube_id = document->actor_id(cube);
    document->select(cube);
    document->select(sphere, SelectionMode::add);
    REQUIRE(document->selection().size() == 2 && document->primary() == sphere);
    document->select(cube, SelectionMode::toggle);
    REQUIRE(document->selection().size() == 1);
    document->select(cube);

    REQUIRE(document->begin_edit("Edit actor details"));
    auto actor = scene->actor(cube);
    REQUIRE(actor);
    actor->transform.x = 13;
    actor->visible = false;
    std::snprintf(actor->label.data(), actor->label.size(), "A cube with spaces");
    REQUIRE(document->preview(cube, *actor) && document->dirty() && !document->can_undo());
    REQUIRE(document->commit_edit() && document->can_undo());
    REQUIRE(std::strcmp(document->undo_label(), "Edit actor details") == 0);
    REQUIRE(document->undo() && !document->dirty() && scene->actor(cube)->visible);
    REQUIRE(document->redo() && document->dirty() && scene->actor(cube)->transform.x == 13);
    REQUIRE(document->group_selection("Architecture"));
    REQUIRE(std::strcmp(scene->actor(cube)->group.data(), "Architecture") == 0);
    REQUIRE(document->set_selected_locked(true) && scene->actor(cube)->locked);
    REQUIRE(!document->erase_selection() && !document->ungroup_selection());
    REQUIRE(document->begin_edit("Locked transform"));
    actor = scene->actor(cube);
    actor->transform.x = 900;
    REQUIRE(!document->preview(cube, *actor));
    REQUIRE(document->cancel_edit());
    REQUIRE(document->save(level) && !document->dirty());
    REQUIRE(document->save(document->path()) && std::strcmp(document->path(), level) == 0);
    REQUIRE(document->load(level));
    cube = document->resolve(cube_id);
    REQUIRE(scene->actor(cube)->locked && std::strcmp(scene->actor(cube)->group.data(), "Architecture") == 0);
    document->select(cube);
    REQUIRE(document->set_selected_locked(false) && document->ungroup_selection());
    REQUIRE(document->save(level) && !document->dirty());
    REQUIRE(document->save(document->path()) && std::strcmp(document->path(), level) == 0);
    REQUIRE(change(*document, *scene, cube, 14));
    REQUIRE(document->undo() && !document->dirty());
    REQUIRE(document->undo() && document->dirty());
    REQUIRE(change(*document, *scene, cube, 20, "Branched edit") && document->dirty());
    REQUIRE(!document->can_redo());
    REQUIRE(document->autosave(autosave) && document->dirty());
    REQUIRE(document->load(level) && !document->dirty() && !document->can_undo());
    cube = document->resolve(cube_id);
    REQUIRE(cube && scene->actor(cube)->transform.x == 13 && !scene->actor(cube)->visible);
    REQUIRE(std::strcmp(scene->actor(cube)->label.data(), "A cube with spaces") == 0);
    REQUIRE(document->recover(autosave) && document->dirty() && !*document->path());
    cube = document->resolve(cube_id);
    REQUIRE(scene->actor(cube)->transform.x == 20);

    // File rejection must preserve the live world, selection, undo chain, and dirty state.
    document->select(cube);
    const auto count = scene->size();
    for (const char *bad :
         {"SOULS_LEVEL 2\nCOUNT 0\nEND\n", "SOULS_LEVEL 1\nCOUNT 257\nEND\n",
          "SOULS_LEVEL 1\nCOUNT 1\nACTOR 1 999 1 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 0 61 -\nEND\n",
          "SOULS_LEVEL 1\nCOUNT 1\nACTOR 1 0 1 0 0 nan 0 0 0 0 0 1 1 1 1 1 1 1 0 61 -\nEND\n",
          "SOULS_LEVEL 1\nCOUNT 2\nACTOR 1 0 1 0 0 0 0 0 0 0 0 1 1 1 1 1 1 100000 0 61 -\nACTOR 1 0 1 0 0 0 "
          "0 0 0 0 0 1 1 1 1 1 1 100000 0 62 -\nEND\n",
          "SOULS_LEVEL 1\nCOUNT 1\nACTOR 1 0 1 0 3 0 0 0 0 0 0 1 1 1 1 1 1 100000 0 61 -\nEND\n",
          "SOULS_LEVEL 1\nCOUNT 1\nACTOR 1 0 1 0 0 0 0 0 0 0 0 0 1 1 1 1 1 100000 0 61 -\nEND\n",
          "SOULS_LEVEL 1\nCOUNT 1\nACTOR 1 0 1 0 0 0 0 0 0 0 0 1 1 1 1 1 1 100000 0 ff -\nEND\n",
          "SOULS_LEVEL 1\nCOUNT 1\nACTOR 1 0 1 0 0 0 0 0 0 0 0 1 1 1 1 1 1 100000 0 6100 -\nEND\n",
          "SOULS_LEVEL 1\nCOUNT 0\nEND\nTRAILING DATA\n"}) {
        REQUIRE(overwrite(malformed, bad));
        REQUIRE(!document->load(malformed));
        REQUIRE(scene->size() == count && document->primary() == cube && document->dirty());
        REQUIRE(scene->actor(cube)->transform.x == 20);
    }
    REQUIRE(document->begin_edit("Cancelled drag"));
    actor = scene->actor(cube);
    actor->transform.x = 999;
    REQUIRE(document->preview(cube, *actor) && document->cancel_edit());
    REQUIRE(scene->actor(cube)->transform.x == 20);
    REQUIRE(document->erase_selection() && !document->resolve(cube_id));
    REQUIRE(document->undo());
    const auto restored = document->resolve(cube_id);
    REQUIRE(restored && restored != cube && !scene->actor(cube));
    REQUIRE(document->primary() == restored && scene->actor(restored)->transform.x == 20);
    cube = restored;
    REQUIRE(document->redo() && !document->resolve(cube_id));
    REQUIRE(document->undo());
    cube = document->resolve(cube_id);

    REQUIRE(document->save(level));
    const bool dirty_before_play = document->dirty();
    REQUIRE(document->begin_play() && document->playing());
    REQUIRE(!document->save(level) && !document->load(level) && !document->undo());
    REQUIRE(change(*document, *scene, cube, 333));
    document->select(cube);
    REQUIRE(document->erase_selection());
    auto temporary = document->spawn(ActorKind::sphere, "Simulation actor");
    REQUIRE(temporary);
    REQUIRE(document->stop_play() && !document->playing());
    cube = document->resolve(cube_id);
    REQUIRE(cube && scene->size() == count && scene->actor(cube)->transform.x == 20);
    REQUIRE(!scene->actor(*temporary) && document->primary() == cube);
    REQUIRE(document->dirty() == dirty_before_play);

    // Bounded history keeps the newest 64 operations and never confuses an evicted save point.
    REQUIRE(document->save(level));
    for (std::size_t i = 0; i < 70; ++i)
        REQUIRE(change(*document, *scene, cube, static_cast<float>(i + 100)));
    std::size_t undos = 0;
    while (document->can_undo()) {
        REQUIRE(document->undo());
        ++undos;
    }
    REQUIRE(undos == EditorDocument::history_limit && document->dirty());
    REQUIRE(scene->actor(document->resolve(cube_id))->transform.x == 105);
    while (document->can_redo())
        REQUIRE(document->redo());
    REQUIRE(scene->actor(document->resolve(cube_id))->transform.x == 169);
    document->select(document->resolve(cube_id));
    REQUIRE(document->duplicate_selection() && scene->size() == count + 1);
    REQUIRE(document->selection().size() == 1 && document->actor_id(document->primary()) != cube_id);
    REQUIRE(document->undo() && scene->size() == count);
    REQUIRE(document->redo() && scene->size() == count + 1);

    REQUIRE(document->new_scene(false) && scene->size() == 0 && document->dirty());
    for (std::size_t i = 0; i < EditorDocument::actor_limit; ++i)
        REQUIRE(document->spawn(ActorKind::cube, "Part", {static_cast<float>(i), 0, 1}));
    REQUIRE(scene->size() == EditorDocument::actor_limit && !document->spawn(ActorKind::cube, "Overflow"));
    REQUIRE(document->save(level) && document->load(level));
    REQUIRE(scene->size() == EditorDocument::actor_limit && !document->dirty());
    document->clear_selection();
    for (auto h : scene->actors())
        document->select(h, SelectionMode::add);
    REQUIRE(document->selection().size() == EditorDocument::actor_limit);
    REQUIRE(!document->duplicate_selection());
    REQUIRE(document->erase_selection() && scene->size() == 0);
    REQUIRE(document->undo() && scene->size() == EditorDocument::actor_limit);
    REQUIRE(document->selection().size() == EditorDocument::actor_limit);
    REQUIRE(document->new_scene() && scene->size() == 5);
    for (auto kind : {ActorKind::cylinder, ActorKind::wedge, ActorKind::capsule, ActorKind::plane})
        REQUIRE(document->spawn(kind, "Built-in primitive"));
    REQUIRE(document->save(level) && document->load(document->path()) && scene->size() == 9);
    std::array<bool, actor_kind_count> found{};
    for (auto h : scene->actors())
        found[static_cast<std::size_t>(scene->actor(h)->kind)] = true;
    for (bool present : found)
        REQUIRE(present);
    REQUIRE(document->new_scene(false));
    for (std::size_t i = 0; i < EditorDocument::actor_limit; ++i)
        REQUIRE(document->spawn(ActorKind::cube, "Part"));
    REQUIRE(document->save(level));
    constexpr char unicode_path[] = "document-\xc3\xa9.souls";
    REQUIRE(document->save(unicode_path) && document->load(unicode_path));
    REQUIRE(!document->save("missing-document-folder/level.souls"));
    REQUIRE(!document->dirty() && std::strcmp(document->path(), unicode_path) == 0);
    REQUIRE(document->load(level) && scene->size() == EditorDocument::actor_limit);
    auto template_scene = Scene::create();
    REQUIRE(template_scene && template_scene->spawn(ActorKind::cube, "Template actor"));
    const auto old_id = document->actor_id(scene->actors()[0]);
    REQUIRE(document->replace_scene(std::move(*template_scene)));
    REQUIRE(scene->size() == 1 && document->dirty() && !document->path()[0] && !document->can_undo());
    REQUIRE(!document->resolve(old_id) && document->selection().empty());
    REQUIRE(!document->replace_scene(std::move(*scene)) && scene->size() == 1);
    std::remove(level);
    std::remove(autosave);
    std::remove(malformed);
    std::puts("Editor document contracts passed");
    return 0;
}
