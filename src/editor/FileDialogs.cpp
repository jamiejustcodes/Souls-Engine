#include "editor/FileDialogs.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

namespace souls::editor {
struct FileDialogs::State {
    // SDL may invoke the callback on another thread, after the workspace closes.
    // The callback and UI each own one reference; neither stores a Workspace pointer.
    std::atomic_uint references{2};
    std::atomic_bool ready{false};
    FileDialogResult result{};
    std::array<char, 1024> location{};
    void release() noexcept {
        if (references.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete this;
    }
    static void SDLCALL callback(void *userdata, const char *const *files, int) noexcept {
        auto *state = static_cast<State *>(userdata);
        if (!files)
            std::snprintf(state->result.error.data(), state->result.error.size(), "%s",
                          *SDL_GetError() ? SDL_GetError() : "The native file dialog failed.");
        else if (!files[0])
            state->result.cancelled = true;
        else if (std::strlen(files[0]) >= state->result.path.size())
            std::snprintf(state->result.error.data(), state->result.error.size(),
                          "The selected path is too long.");
        else
            std::snprintf(state->result.path.data(), state->result.path.size(), "%s", files[0]);
        state->ready.store(true, std::memory_order_release);
        state->release();
    }
};
FileDialogs::~FileDialogs() {
    if (pending_)
        pending_->release();
}
Result<void> FileDialogs::request(FileDialogKind kind, SDL_Window *window, const char *location) noexcept {
    if (pending_)
        return std::unexpected(Error{ErrorCode::invalid_argument, "A file dialog is already open."});
    auto *state = new (std::nothrow) State;
    if (!state)
        return std::unexpected(Error{ErrorCode::exhausted, "File dialog storage allocation failed."});
    state->result.kind = kind;
    std::snprintf(state->location.data(), state->location.size(), "%s", location ? location : "");
    pending_ = state;
    static const SDL_DialogFileFilter filters[]{{"Souls levels", "souls"}, {"All files", "*"}};
    if (kind == FileDialogKind::save)
        SDL_ShowSaveFileDialog(State::callback, state, window, filters, 2, state->location.data());
    else
        SDL_ShowOpenFileDialog(State::callback, state, window, filters, 2, state->location.data(), false);
    return {};
}
bool FileDialogs::poll(FileDialogResult &result) noexcept {
    if (!pending_ || !pending_->ready.load(std::memory_order_acquire))
        return false;
    result = pending_->result;
    pending_->release();
    pending_ = nullptr;
    return true;
}
} // namespace souls::editor
