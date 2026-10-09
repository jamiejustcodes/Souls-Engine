#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <souls/scene/Scene.hpp>

namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
} // namespace
int main() {
    using namespace souls;
    for (auto kind : {ActorKind::cube, ActorKind::sphere, ActorKind::cylinder, ActorKind::wedge,
                      ActorKind::capsule, ActorKind::plane}) {
        auto scene = Scene::create();
        check(scene.has_value(), "create primitive scene");
        auto h = scene->spawn(kind, "Part");
        check(h.has_value(), "spawn built-in part");
        Camera camera;
        camera.position = kind == ActorKind::plane ? Vec3{0, 0, 5} : Vec3{0, -8, 0};
        const Vec3 ray = kind == ActorKind::plane ? Vec3{0, 0, -1} : Vec3{0, 1, 0};
        check(scene->pick(camera, ray) == *h, "pick the rendered primitive shape");
        auto actor = scene->actor(*h);
        actor->material = 2;
        actor->color = {0.2F, 0.3F, 0.4F};
        std::snprintf(actor->group.data(), actor->group.size(), "Castle");
        check(scene->update(*h, *actor).has_value(), "edit material and group");
        std::array<std::byte, 256> storage{};
        LinearArena arena{storage};
        auto batch = scene->extract(arena);
        check(batch && batch->material[0] == 2, "material metadata and render column agree");
        check(std::strcmp(scene->actor(*h)->group.data(), "Castle") == 0, "Flecs folder survives inspection");
        actor->locked = true;
        check(scene->update(*h, *actor).has_value(), "lock actor");
        check(!scene->pick(camera, ray), "locked actors cannot intercept viewport selection");
    }
    auto scene = Scene::create();
    auto capsule = scene->spawn(ActorKind::capsule, "Capsule");
    check(capsule.has_value(), "spawn capsule for silhouette check");
    Camera camera;
    camera.position = {0.75F, -8, 0};
    check(!scene->pick(camera, {0, 1, 0}), "capsule picking respects its half-unit radius");
    check(scene->remove(*capsule).has_value(), "remove capsule");
    auto wedge = scene->spawn(ActorKind::wedge, "Wedge");
    check(wedge.has_value(), "spawn wedge for slope check");
    camera.position = {-0.5F, -8, 0.5F};
    check(!scene->pick(camera, {0, 1, 0}), "wedge picking rejects space above its sloping face");
    std::puts("Built-in primitive picking, folders, locks and material columns passed");
}
