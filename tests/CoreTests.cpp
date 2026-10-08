#include <souls/core/Core.hpp>
#include <cstdio>
#include <cstdlib>
namespace { void check(bool value, const char* what) { if (!value) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } } struct Tag; }
int main() {
    alignas(64) std::array<std::byte, 512> storage{};
    souls::LinearArena arena{std::span{storage}.subspan(1)};
    auto a = arena.allocate(32, 64); check(a.has_value(), "aligned allocation");
    check(reinterpret_cast<std::uintptr_t>(a->data()) % 64 == 0, "alignment uses actual base address");
    const auto used = arena.used();
    check(!arena.allocate(1024), "capacity exhaustion"); check(arena.used() == used, "failed allocation is atomic");
    check(!arena.allocate(1, 3), "non power of two alignment"); check(!arena.allocate(1, 0), "zero alignment");
    check(!arena.allocate(std::numeric_limits<std::size_t>::max(), 64), "overflow guarded");
    arena.reset(); check(arena.used() == 0 && arena.high_water() == used, "reset and high water");
    souls::Pool<int, Tag, 2> pool;
    auto h1 = pool.emplace(17), h2 = pool.emplace(42); check(h1 && h2, "pool capacity");
    check(!pool.emplace(9), "pool exhaustion"); check(*pool.get(*h1) == 17, "value lookup");
    check(pool.erase(*h1).has_value(), "erase live"); check(!pool.get(*h1), "stale generation rejected");
    auto h3 = pool.emplace(99); check(h3 && h3->index == h1->index && h3->generation != h1->generation, "generation advances on reuse");
    check(!pool.erase(*h1), "double free rejected");
    check(!pool.get(souls::Handle<Tag>{999, 1}), "index bounds"); check(!pool.get({}), "null handle");
    check(pool.size() == 2, "pool live count");
    const auto& const_pool = pool; check(*const_pool.get(*h3) == 99, "const lookup");
    std::puts("Core contracts passed");
}
