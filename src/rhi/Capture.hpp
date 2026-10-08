#pragma once
#include <cstdio>
#include <souls/core/Core.hpp>
#include <vector>
namespace souls::rhi::detail {
inline Result<void> write_bmp(const char *path, const std::byte *pixels, std::uint32_t width,
                              std::uint32_t height, std::size_t pitch, bool bgra = false) noexcept {
    if (!path || !width || !height || width > 16384 || height > 16384)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid capture dimensions"});
    const std::uint32_t stride = (width * 3 + 3) & ~3U, size = stride * height + 54;
    std::array<unsigned char, 54> header{};
    header[0] = 'B';
    header[1] = 'M';
    const auto put = [&](std::size_t offset, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i)
            header[offset + i] = static_cast<unsigned char>(value >> (i * 8));
    };
    put(2, size);
    put(10, 54);
    put(14, 40);
    put(18, width);
    put(22, height);
    header[26] = 1;
    header[28] = 24;
    FILE *file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "wb") != 0)
        file = nullptr;
#else
    file = std::fopen(path, "wb");
#endif
    if (!file)
        return std::unexpected(Error{ErrorCode::platform, "Cannot open capture output"});
    bool ok = std::fwrite(header.data(), 1, header.size(), file) == header.size();
    std::vector<unsigned char> row(stride);
    for (std::uint32_t y = height; y > 0; --y) {
        auto *src = reinterpret_cast<const unsigned char *>(pixels + (y - 1) * pitch);
        for (std::uint32_t x = 0; x < width; ++x) {
            row[x * 3] = src[x * 4 + (bgra ? 0 : 2)];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + (bgra ? 2 : 0)];
        }
        ok = std::fwrite(row.data(), 1, row.size(), file) == row.size() && ok;
    }
    ok = std::fclose(file) == 0 && ok;
    if (!ok)
        return std::unexpected(Error{ErrorCode::platform, "Capture write failed"});
    return {};
}
} // namespace souls::rhi::detail
