#pragma once
#include <memory>
#include <souls/core/Core.hpp>
namespace souls::brand {
struct ImageDeleter {
    void operator()(unsigned char *pixels) const noexcept;
};
// The original artwork is embedded; launch never depends on the working directory.
struct Logo {
    std::unique_ptr<unsigned char, ImageDeleter> pixels;
    std::uint32_t width = 0, height = 0;
    [[nodiscard]] std::span<const std::byte> rgba() const noexcept {
        return {reinterpret_cast<const std::byte *>(pixels.get()),
                static_cast<std::size_t>(width) * height * 4};
    }
    [[nodiscard]] static Result<Logo> load() noexcept;
};
} // namespace souls::brand
