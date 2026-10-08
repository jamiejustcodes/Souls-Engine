#pragma once
#include <SDL3/SDL.h>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <souls/rhi/SoulsRHI.hpp>
namespace souls::platform {
struct Options {
    std::uint32_t smoke_frames = 0;
    bool validation = false;
    bool vsync = true;
    const char *capture = nullptr;
};
inline Result<Options> options(int argc, char **argv) noexcept {
    Options result;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--validation") == 0)
            result.validation = true;
        else if (std::strcmp(argv[i], "--no-vsync") == 0)
            result.vsync = false;
        else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc)
            result.capture = argv[++i];
        else if (std::strcmp(argv[i], "--smoke") == 0 && i + 1 < argc) {
            const auto *value = argv[++i];
            const auto *end = value + std::strlen(value);
            const auto parse = std::from_chars(value, end, result.smoke_frames);
            if (parse.ec != std::errc{} || parse.ptr != end || result.smoke_frames < 90)
                return std::unexpected(
                    Error{ErrorCode::invalid_argument, "--smoke requires at least 90 frames"});
        } else
            return std::unexpected(
                Error{ErrorCode::invalid_argument, "Usage: [--validation] [--no-vsync] [--smoke N>=90]"});
    }
    return result;
}
inline int report(Error error) noexcept {
    std::fprintf(stderr, "Souls: %s (native %lld)\n", error.message, static_cast<long long>(error.native));
    return 1;
}
class Window final {
  public:
    Window() noexcept = default;
    ~Window() {
        if (window_)
            SDL_DestroyWindow(window_);
        if (initialized_)
            SDL_Quit();
    }
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    [[nodiscard]] Result<void> create(const char *title) noexcept {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
            return std::unexpected(Error{ErrorCode::platform, SDL_GetError()});
        initialized_ = true;
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(SOULS_RHI_VULKAN)
        flags |= SDL_WINDOW_VULKAN;
#endif
        window_ = SDL_CreateWindow(title, 1600, 960, flags);
        if (!window_)
            return std::unexpected(Error{ErrorCode::platform, SDL_GetError()});
        const float scale = SDL_GetWindowDisplayScale(window_);
        if (!SDL_SetWindowMinimumSize(window_, static_cast<int>(1100 * scale), static_cast<int>(720 * scale)))
            return std::unexpected(Error{ErrorCode::platform, SDL_GetError()});
        return {};
    }
    [[nodiscard]] SDL_Window *get() const noexcept {
        return window_;
    }
    [[nodiscard]] rhi::Extent pixels() const noexcept {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED)
            return {};
        return {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
    }

  private:
    SDL_Window *window_ = nullptr;
    bool initialized_ = false;
};
// A smoke run drives both resize directions through normal SDL events.
inline bool smoke_resize(SDL_Window *window, std::uint32_t frame) noexcept {
    const float scale = SDL_GetWindowDisplayScale(window);
    if (frame == 30)
        return SDL_SetWindowSize(window, static_cast<int>(1100 * scale), static_cast<int>(720 * scale));
    if (frame == 60)
        return SDL_SetWindowSize(window, static_cast<int>(1600 * scale), static_cast<int>(960 * scale));
    return true;
}
} // namespace souls::platform
