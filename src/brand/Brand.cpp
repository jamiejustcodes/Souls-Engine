#include "BrandData.hpp"
#include <souls/brand/Brand.hpp>
#include <stb_image.h>
namespace souls::brand {
void ImageDeleter::operator()(unsigned char *pixels) const noexcept {
    stbi_image_free(pixels);
}
Result<Logo> Logo::load() noexcept {
    int width = 0, height = 0, channels = 0;
    auto *pixels = stbi_load_from_memory(embedded::logo, static_cast<int>(sizeof(embedded::logo)), &width,
                                         &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        stbi_image_free(pixels);
        return std::unexpected(Error{ErrorCode::platform, "The embedded Souls logo could not be decoded."});
    }
    Logo result;
    result.pixels.reset(pixels);
    result.width = static_cast<std::uint32_t>(width);
    result.height = static_cast<std::uint32_t>(height);
    return result;
}
} // namespace souls::brand
