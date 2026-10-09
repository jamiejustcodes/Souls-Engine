#include <cstdio>
#include <souls/brand/Brand.hpp>
int main() {
    auto logo = souls::brand::Logo::load();
    if (!logo || logo->width != 1024 || logo->height != 687 || logo->rgba().size() != 1024 * 687 * 4)
        return 1;
    std::printf("Logo %u x %u\n", logo->width, logo->height);
    const auto *pixels = logo->pixels.get();
    // The source's white page and black crest must survive embedding/PNG decoding.
    if (pixels[0] < 245 || pixels[1] < 245 || pixels[2] < 245 || pixels[3] != 255)
        return 1;
    const auto black = (273 * 1024 + 385) * 4;
    std::printf("Crest: %u %u %u\n", pixels[black], pixels[black + 1], pixels[black + 2]);
    if (pixels[black] > 16 || pixels[black + 1] > 16 || pixels[black + 2] > 16)
        return 1;
    std::puts("Original Souls logo embedding and RGBA decoding passed");
}
