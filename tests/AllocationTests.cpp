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
    auto *p = std::malloc(bytes ? bytes : 1);
    if (!p)
        std::abort();
    return p;
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
    return allocate(bytes);
}
void *operator new[](std::size_t bytes) {
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
void *operator new(std::size_t bytes, std::align_val_t alignment) {
    count();
    const auto align = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
    auto *ptr = _aligned_malloc(bytes ? bytes : 1, align);
#else
    const auto padded = (bytes + align - 1) / align * align;
    auto *ptr = std::aligned_alloc(align, padded ? padded : align);
#endif
    if (!ptr)
        std::abort();
    return ptr;
}
void *operator new[](std::size_t bytes, std::align_val_t alignment) {
    return ::operator new(bytes, alignment);
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
int main() {
    auto scene = souls::Scene::create();
    if (!scene)
        return 1;
    for (std::uint32_t i = 0; i < 128; ++i)
        if (!scene->add({static_cast<float>(i), 0, 0}, 1, i))
            return 2;
    std::array<std::byte, 16384> storage{};
    souls::LinearArena arena{storage};
    auto warm = scene->extract(arena);
    if (!warm)
        return 3;
    Monitor monitor;
    measuring = true;
    for (int frame = 0; frame < 2000; ++frame) {
        // Live value edits must keep the existing Flecs archetype and its storage.
        auto h = scene->actors()[0];
        auto actor = scene->actor(h);
        actor->transform.rotation.z = static_cast<float>(frame);
        if (!scene->update(h, *actor))
            return 6;
        arena.reset();
        auto batch = scene->extract(arena);
        if (!batch || batch->x.size() != 128 || batch->material[127] != 127)
            return 4;
    }
    measuring = false;
    std::printf("2000 frame extractions: %zu C++/Flecs allocation attempts\n", attempts);
    return attempts ? 5 : 0;
}
