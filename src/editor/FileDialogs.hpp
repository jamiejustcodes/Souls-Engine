#pragma once
#include <souls/core/Core.hpp>
struct SDL_Window;
namespace souls::editor {
enum class FileDialogKind { open, save };
struct FileDialogResult {
    FileDialogKind kind{};
    bool cancelled = false;
    std::array<char, 1024> path{};
    std::array<char, 160> error{};
};
class FileDialogs final {
  public:
    FileDialogs() noexcept = default;
    ~FileDialogs();
    FileDialogs(const FileDialogs &) = delete;
    FileDialogs &operator=(const FileDialogs &) = delete;
    [[nodiscard]] Result<void> request(FileDialogKind kind, SDL_Window *window,
                                       const char *location) noexcept;
    [[nodiscard]] bool poll(FileDialogResult &result) noexcept;
    [[nodiscard]] bool pending() const noexcept {
        return pending_ != nullptr;
    }

  private:
    struct State;
    State *pending_ = nullptr;
};
} // namespace souls::editor
