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
int main() {
    // Prove the counter sees all forms before trusting a zero-allocation result.
    constexpr auto aligned = std::align_val_t{64};
    measuring = true;
    void *probes[]{::operator new(32),
                   ::operator new[](32),
                   ::operator new(32, std::nothrow),
                   ::operator new[](32, std::nothrow),
                   ::operator new(64, aligned),
                   ::operator new[](64, aligned),
                   ::operator new(64, aligned, std::nothrow),
                   ::operator new[](64, aligned, std::nothrow)};
    measuring = false;
    if (attempts != std::size(probes))
        return 7;
    for (auto *p : probes)
        if (!p)
            return 8;
    for (std::size_t i = 4; i < std::size(probes); ++i)
        if (reinterpret_cast<std::uintptr_t>(probes[i]) % 64 != 0)
            return 9;
    ::operator delete(probes[0]);
    ::operator delete[](probes[1]);
    ::operator delete(probes[2], std::nothrow);
    ::operator delete[](probes[3], std::nothrow);
    ::operator delete(probes[4], std::size_t{64}, aligned);
    ::operator delete[](probes[5], std::size_t{64}, aligned);
    ::operator delete(probes[6], aligned, std::nothrow);
    ::operator delete[](probes[7], aligned, std::nothrow);
    attempts = 0;
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
