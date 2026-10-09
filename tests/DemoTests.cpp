#include <cstdio>
#include <cstdlib>
#include <flecs.h>
#include <new>
#include <souls/scene/Scene.hpp>
#if defined(_WIN32)
#include <malloc.h>
#endif
namespace {
bool measuring = false;
std::size_t attempts = 0;
void count() noexcept {
    if (measuring)
        ++attempts;
}
void *allocate(std::size_t bytes) noexcept {
    count();
    return std::malloc(bytes ? bytes : 1);
}
void *require_allocation(void *p) noexcept {
    if (!p)
        std::abort();
    return p;
}
void *allocate_aligned(std::size_t bytes, std::align_val_t alignment) noexcept {
    count();
    const auto align = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
    return _aligned_malloc(bytes ? bytes : 1, align);
#else
    if (bytes > std::numeric_limits<std::size_t>::max() - (align - 1))
        return nullptr;
    const auto padded = (bytes + align - 1) / align * align;
    return std::aligned_alloc(align, padded ? padded : align);
#endif
}
ecs_os_api_malloc_t original_malloc = nullptr;
ecs_os_api_calloc_t original_calloc = nullptr;
ecs_os_api_realloc_t original_realloc = nullptr;
void *monitored_malloc(ecs_size_t size) {
    count();
    return original_malloc(size);
}
void *monitored_calloc(ecs_size_t size) {
    count();
    return original_calloc(size);
}
void *monitored_realloc(void *ptr, ecs_size_t size) {
    count();
    return original_realloc(ptr, size);
}
struct Monitor {
    Monitor() {
        original_malloc = ecs_os_api.malloc_;
        original_calloc = ecs_os_api.calloc_;
        original_realloc = ecs_os_api.realloc_;
        ecs_os_api.malloc_ = monitored_malloc;
        ecs_os_api.calloc_ = monitored_calloc;
        ecs_os_api.realloc_ = monitored_realloc;
    }
    ~Monitor() {
        measuring = false;
        ecs_os_api.malloc_ = original_malloc;
        ecs_os_api.calloc_ = original_calloc;
        ecs_os_api.realloc_ = original_realloc;
    }
};
} // namespace
void *operator new(std::size_t bytes) {
    return require_allocation(allocate(bytes));
}
void *operator new[](std::size_t bytes) {
    return require_allocation(allocate(bytes));
}
// ASan supplies its own nothrow overloads unless we replace them too. Every
// allocation form must use the same allocator as our replacement delete hooks.
void *operator new(std::size_t bytes, const std::nothrow_t &) noexcept {
    return allocate(bytes);
}
void *operator new[](std::size_t bytes, const std::nothrow_t &) noexcept {
    return allocate(bytes);
}
void operator delete(void *ptr) noexcept {
    std::free(ptr);
}
void operator delete[](void *ptr) noexcept {
    std::free(ptr);
}
void operator delete(void *ptr, std::size_t) noexcept {
    std::free(ptr);
}
void operator delete[](void *ptr, std::size_t) noexcept {
    std::free(ptr);
}
void operator delete(void *ptr, const std::nothrow_t &) noexcept {
    std::free(ptr);
}
void operator delete[](void *ptr, const std::nothrow_t &) noexcept {
    std::free(ptr);
}
void *operator new(std::size_t bytes, std::align_val_t alignment) {
    return require_allocation(allocate_aligned(bytes, alignment));
}
void *operator new[](std::size_t bytes, std::align_val_t alignment) {
    return ::operator new(bytes, alignment);
}
void *operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t &) noexcept {
    return allocate_aligned(bytes, alignment);
}
void *operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t &) noexcept {
    return allocate_aligned(bytes, alignment);
}
void operator delete(void *ptr, std::align_val_t) noexcept {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}
void operator delete[](void *ptr, std::align_val_t alignment) noexcept {
    ::operator delete(ptr, alignment);
}
void operator delete(void *ptr, std::size_t, std::align_val_t alignment) noexcept {
    ::operator delete(ptr, alignment);
}
void operator delete[](void *ptr, std::size_t, std::align_val_t alignment) noexcept {
    ::operator delete(ptr, alignment);
}
void operator delete(void *ptr, std::align_val_t alignment, const std::nothrow_t &) noexcept {
    ::operator delete(ptr, alignment);
}
void operator delete[](void *ptr, std::align_val_t alignment, const std::nothrow_t &) noexcept {
    ::operator delete(ptr, alignment);
}
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <souls/demo/Demo.hpp>

namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
bool close(float a, float b, float epsilon = 0.002F) {
    return std::abs(a - b) < epsilon;
}
void advance(souls::demo::Session &session, souls::demo::Input input, unsigned ticks) {
    for (unsigned i = 0; i < ticks; ++i)
        check(session.tick(input, souls::demo::Session::fixed_step).has_value(), "advance fixed demo tick");
}
void walk_to(souls::demo::Session &session, float y) {
    unsigned ticks = 0;
    while (session.state().position.y < y && ticks++ < 600)
        advance(session, {1}, 1);
    check(session.state().position.y >= y, "reach walking target");
}
void leap_to(souls::demo::Session &session, float y) {
    advance(session, {1, 0, true}, 1);
    walk_to(session, y);
    advance(session, {}, 100);
    check(session.state().grounded, "land on destination platform");
}
void allocation_contract(souls::demo::Session &session) {
    check(session.reset().has_value(), "reset before allocation measurement");
    Monitor monitor;
    attempts = 0;
    measuring = true;
    void *cpp_probe = ::operator new(32);
    void *flecs_probe = ecs_os_malloc(32);
    measuring = false;
    check(attempts == 2, "allocation monitor observes C++ and Flecs requests");
    ::operator delete(cpp_probe);
    ecs_os_free(flecs_probe);
    attempts = 0;
    measuring = true;
    walk_to(session, 7.9F);
    leap_to(session, 10.9F);
    walk_to(session, 11.45F);
    leap_to(session, 14.25F);
    walk_to(session, 14.9F);
    leap_to(session, 17.7F);
    check(session.reset().has_value(), "restart under allocation measurement");
    advance(session, {0, 1, false, true}, 120);
    advance(session, {}, 120);
    measuring = false;
    check(attempts == 0, "movement, collectibles, completion and restart allocate no C++ or Flecs storage");
}
} // namespace

int main() {
    using namespace souls;
    using namespace souls::demo;
    auto scene = Scene::create();
    check(scene.has_value(), "create demo scene");
    auto session = Session::create(*scene);
    check(session.has_value(), "populate playable courtyard");
    check(scene->size() > 25 && scene->size() < 100, "course fits bounded actor capacity");
    check(scene->actor(session->player_handle())->kind == ActorKind::capsule, "player has capsule visual");
    check(!Session::create(*scene), "reject nonempty world without replacing its actors");
    const auto size = scene->size();
    // Joined platform tops must meet edge-to-edge: coplanar overlap produces
    // visible depth fighting even when the controller crosses them correctly.
    for (auto first : scene->actors()) {
        const auto a = scene->actor(first);
        if (a->kind != ActorKind::cube)
            continue;
        for (auto second : scene->actors()) {
            if (first == second)
                continue;
            const auto b = scene->actor(second);
            if (b->kind != ActorKind::cube)
                continue;
            const auto &u = a->transform;
            const auto &v = b->transform;
            const bool same_top = std::abs(u.z + u.scale.z - v.z - v.scale.z) < 0.0001F;
            const bool overlap_x =
                std::min(u.x + u.scale.x, v.x + v.scale.x) - std::max(u.x - u.scale.x, v.x - v.scale.x) >
                0.0001F;
            const bool overlap_y =
                std::min(u.y + u.scale.y, v.y + v.scale.y) - std::max(u.y - u.scale.y, v.y - v.scale.y) >
                0.0001F;
            check(!(same_top && overlap_x && overlap_y), "authored platform tops do not fight for depth");
        }
    }
    advance(*session, {}, 120);
    check(session->state().grounded && close(session->state().position.z, 0.851F),
          "gravity rests on courtyard");
    check(close(static_cast<float>(session->state().elapsed), 1), "fixed-step elapsed clock");
    advance(*session, {1}, 60);
    check(close(session->state().position.y, 1.4F), "camera-relative forward movement");
    check(session->reset().has_value(), "restart movement test");
    advance(*session, {0, 1, false, true}, 180);
    check(close(session->state().position.x, 2.879F), "swept AABB stops at thin courtyard wall");
    check(close(session->state().velocity.x, 0), "wall clears blocked velocity");
    check(session->reset().has_value(), "restart jump test");
    advance(*session, {0, 0, true}, 1);
    check(!session->state().grounded && session->state().velocity.z > 7, "grounded jump launches player");
    advance(*session, {0, 0, true}, 180);
    check(session->state().grounded && close(session->state().position.z, 0.851F),
          "held jump cannot repeatedly launch");
    advance(*session, {}, 1);
    advance(*session, {0, 0, true}, 1);
    check(!session->state().grounded, "jump rearms on release");
    check(session->reset().has_value(), "restart route");
    walk_to(*session, 7.9F);
    check(session->state().checkpoint == 1 && session->state().collected == 1,
          "checkpoint activates and orb collects");
    leap_to(*session, 10.9F);
    check(session->state().collected == 2 && close(session->state().position.z, 1.401F),
          "first leap reaches raised stone");
    walk_to(*session, 11.45F);
    leap_to(*session, 14.25F);
    check(session->state().collected == 3 && close(session->state().position.z, 1.951F),
          "second leap reaches higher stone");
    walk_to(*session, 14.9F);
    leap_to(*session, 17.7F);
    check(session->state().won && session->state().collected == 4,
          "course can be completed using public movement input");
    const auto elapsed = session->state().elapsed;
    advance(*session, {1}, 60);
    check(session->state().elapsed == elapsed, "winning freezes completion time");
    check(session->reset().has_value(), "restart winning course");
    check(!session->state().won && session->state().collected == 0 && session->state().checkpoint == 0 &&
              session->state().falls == 0 && session->state().elapsed == 0 && scene->size() == size,
          "restart resets rules without recreating actors");
    walk_to(*session, 7);
    advance(*session, {0, 1}, 240);
    advance(*session, {}, 180);
    check(session->state().falls >= 1 && close(session->state().position.y, 6.8F) &&
              session->state().checkpoint == 1 && session->state().collected == 1,
          "fall respawns at checkpoint and retains collected orbs");
    check(session->reset().has_value(), "reset fixed-step comparison");
    auto second_scene = Scene::create();
    auto second = Session::create(*second_scene);
    advance(*session, {1}, 60);
    for (unsigned i = 0; i < 30; ++i)
        check(second->tick({1}, 2 * Session::fixed_step).has_value(), "advance two ticks per frame");
    check(close(session->state().position.y, second->state().position.y) &&
              close(session->state().position.z, second->state().position.z),
          "render cadence does not change simulation trajectory");
    const auto before = session->state();
    check(!session->tick({}, -1) && !session->tick({}, std::numeric_limits<float>::quiet_NaN()) &&
              !session->tick({2}, 0.1F) &&
              !session->tick({0, 0, false, false, std::numeric_limits<float>::infinity()}, 0.1F),
          "reject invalid timing and controls");
    check(close(session->state().position.y, before.position.y), "invalid input does not mutate gameplay");
    check(session->tick({0, 0, false, false, pi / 2, -100}, 0).has_value(), "look applies once per frame");
    check(close(session->camera().pitch, -0.95F), "camera pitch is constrained");
    check(session->reset().has_value(), "reset camera-relative movement");
    check(session->tick({0, 0, false, false, -pi / 2, 0}, 0).has_value(), "orbit camera quarter turn");
    advance(*session, {1}, 30);
    check(session->state().position.x > 1 && close(session->state().position.y, -1),
          "movement follows orbit yaw");
    const auto before_hitch = session->state().elapsed;
    check(session->tick({}, 20).has_value(), "large hitch is bounded");
    check(session->state().elapsed - before_hitch <= 0.251, "catch-up work is bounded to thirty ticks");
    allocation_contract(*session);
    check(scene->remove(session->player_handle()).has_value(),
          "remove borrowed player for stale-handle check");
    check(!session->tick({}, 0), "stale scene contents return an explicit error");
    std::puts("Playable courtyard movement, collision, jumping, checkpoints, completion and fixed-step "
              "contracts passed");
}
