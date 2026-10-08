#include <cmath>
#include <cstdio>
#include <souls/scene/Scene.hpp>
int main() {
    auto scene = souls::Scene::create();
    if (!scene)
        return 1;
    auto a = scene->add({1, 2, 3}, 4, 5);
    auto b = scene->add({6, 7, 8}, 9, 10);
    if (!a || !b || scene->size() != 2)
        return 2;
    std::array<std::byte, 256> storage{};
    souls::LinearArena arena{storage};
    auto batch = scene->extract(arena);
    if (!batch || batch->x.size() != 2 || batch->x[0] != 1 || batch->z[1] != 8 || batch->material[1] != 10)
        return 3;
    if (!scene->remove(*a) || scene->remove(*a))
        return 4;
    auto c = scene->add({11, 12, 13}, 14, 15);
    if (!c || c->generation == a->generation)
        return 5;
    arena.reset();
    batch = scene->extract(arena);
    if (!batch || batch->x.size() != 2)
        return 6;
    auto world = souls::Scene::create();
    if (!world || !world->playground() || world->size() != 5)
        return 7;
    souls::EntityHandle cube{}, sphere{};
    for (auto h : world->actors()) {
        auto actor = world->actor(h);
        if (actor->kind == souls::ActorKind::cube)
            cube = h;
        if (actor->kind == souls::ActorKind::sphere)
            sphere = h;
    }
    souls::Camera camera;
    camera.position = {-2, -8, 1};
    if (world->pick(camera, {0, 1, 0}) != cube)
        return 8;
    auto edited = world->actor(cube);
    edited->transform.rotation = {0, 0, 90};
    edited->transform.scale = {0.5F, 2, 1};
    edited->transform.x = -3;
    if (!world->update(cube, *edited) || world->actor(cube)->transform.rotation.z != 90)
        return 9;
    camera.position = {-3, -8, 1};
    if (world->pick(camera, {0, 1, 0}) != cube)
        return 10;
    edited->visible = false;
    if (!world->update(cube, *edited) || world->pick(camera, {0, 1, 0}))
        return 11;
    auto copy = world->duplicate(cube);
    if (!copy || world->actor(*copy)->transform.x != -1 || world->actor(*copy)->visible)
        return 12;
    auto bad = *edited;
    bad.transform.scale.x = 0;
    if (world->update(cube, bad))
        return 13;
    if (!world->remove(*copy) || world->actor(*copy))
        return 14;
    std::array<std::byte, 2048> frame_storage{};
    souls::LinearArena frame_arena{frame_storage};
    auto rendered = world->extract(frame_arena);
    if (!rendered)
        return 15;
    bool found = false;
    for (std::size_t i = 0; i < rendered->handles.size(); ++i)
        if (rendered->handles[i] == cube) {
            found = true;
            if (rendered->rotation[2][i] != 90 || rendered->scale[1][i] != 2 || rendered->visible[i] != 0)
                return 16;
        }
    if (!found)
        return 17;
    camera.position = {2, -8, 1};
    if (world->pick(camera, {0, 1, 0}) != sphere)
        return 18;
    camera = souls::Camera{};
    auto forward = camera.view();
    auto p = souls::transform_point(forward, camera.position + camera.forward() * 3);
    if (std::abs(p.x) > 0.001F || std::abs(p.y) > 0.001F || std::abs(p.z - 3) > 0.001F)
        return 19;
    auto projection = camera.projection(16.0F / 9);
    auto near = souls::transform_point(projection, {0, 0, 0.1F});
    auto far = souls::transform_point(projection, {0, 0, 1000});
    if (std::abs(near.z) > 0.001F || std::abs(far.z - 1) > 0.001F)
        return 20;
    std::puts("Flecs SoA extraction and entity contracts passed");
}
