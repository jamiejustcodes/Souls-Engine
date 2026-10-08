#include <souls/core/Core.hpp>
#include <chrono>
#include <cstdio>
int main() {
    alignas(64) std::array<std::byte, 65536> storage{};
    souls::LinearArena arena{storage};
    constexpr std::size_t iterations = 1000000;
    std::uintptr_t checksum = 0;
    auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        if ((i & 255) == 0) arena.reset();
        auto block = arena.allocate(128, 64);
        if (!block) return 1;
        checksum ^= reinterpret_cast<std::uintptr_t>(block->data()) + i;
    }
    auto elapsed = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    std::printf("arena: %.2f ns/allocation; %zu allocations; high water %zu; checksum %llu\n", elapsed / static_cast<double>(iterations), iterations, arena.high_water(), static_cast<unsigned long long>(checksum));
}
