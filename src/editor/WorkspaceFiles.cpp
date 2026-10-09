#include "editor/Workspace.hpp"
#include <SDL3/SDL.h>
#include <cctype>
#include <cstdio>
#include <cstring>
namespace souls::editor {
void Workspace::initialize(SDL_Window *window, bool persistent) noexcept {
    main_window_ = window;
    persistent_ = persistent;
    if (!persistent_)
        return;
    if (char *directory = SDL_GetPrefPath("SoulsEngine", "SoulsEditor")) {
        std::snprintf(recovery_path_.data(), recovery_path_.size(), "%srecovery.souls", directory);
        SDL_free(directory);
        SDL_PathInfo info{};
        recovery_available_ = SDL_GetPathInfo(recovery_path_.data(), &info) && info.type == SDL_PATHTYPE_FILE;
    } else
        log(SDL_GetError());
}
void Workspace::request_exit() noexcept {
    request_action(Action::quit);
}
void Workspace::request_action(Action action) noexcept {
    if (files_.pending() || unsaved_prompt_)
        return;
    finish_edit();
    if (playing_)
        stop(scene_);
    if (flying_) {
        if (auto *window = SDL_GetWindowFromID(flight_window_))
            SDL_SetWindowRelativeMouseMode(window, false);
        flying_ = false;
    }
    if (document_.dirty()) {
        pending_action_ = action;
        unsaved_prompt_ = true;
    } else
        perform_action(action);
}
void Workspace::perform_action(Action action) noexcept {
    pending_action_ = Action::none;
    if (action == Action::quit)
        quit_ = true;
    else if (action == Action::new_level) {
        auto done = document_.new_scene();
        if (!done)
            log(done.error().message);
        else {
            selected_ = {};
            camera_ = Camera{};
            log("New playground level");
        }
    } else if (action == Action::open_level) {
        auto done = files_.request(FileDialogKind::open, main_window_, document_.path());
        if (!done)
            log(done.error().message);
    }
}
void Workspace::save_level(bool save_as) noexcept {
    if (playing_ || files_.pending())
        return;
    finish_edit();
    if (save_as || !document_.path()[0]) {
        auto done = files_.request(FileDialogKind::save, main_window_,
                                   document_.path()[0] ? document_.path() : "Untitled.souls");
        if (!done) {
            log(done.error().message);
            after_save_ = Action::none;
        }
    } else {
        auto done = document_.save(document_.path());
        if (!done) {
            log(done.error().message);
            after_save_ = Action::none;
        } else {
            log("Level saved");
            if (recovery_path_[0])
                SDL_RemovePath(recovery_path_.data());
            const auto action = after_save_;
            after_save_ = Action::none;
            perform_action(action);
        }
    }
}
void Workspace::file_actions() noexcept {
    FileDialogResult result{};
    if (files_.poll(result)) {
        if (result.error[0]) {
            log(result.error.data());
            after_save_ = Action::none;
        } else if (result.cancelled)
            after_save_ = Action::none;
        else if (result.kind == FileDialogKind::open) {
            auto done = document_.load(result.path.data());
            if (!done)
                log(done.error().message);
            else {
                selected_ = document_.primary();
                camera_ = Camera{};
                log("Level opened");
            }
        } else {
            const auto length = std::strlen(result.path.data());
            const char *suffix = length >= 6 ? result.path.data() + length - 6 : "";
            bool extension = length >= 6;
            constexpr char expected[] = ".souls";
            for (std::size_t i = 0; extension && i < 6; ++i)
                extension = std::tolower(static_cast<unsigned char>(suffix[i])) == expected[i];
            if (!extension && length + 6 < result.path.size())
                std::memcpy(result.path.data() + length, expected, 7);
            auto done = document_.save(result.path.data());
            if (!done) {
                log(done.error().message);
                after_save_ = Action::none;
            } else {
                log("Level saved");
                if (recovery_path_[0])
                    SDL_RemovePath(recovery_path_.data());
                const auto action = after_save_;
                after_save_ = Action::none;
                perform_action(action);
            }
        }
    }
    if (unsaved_prompt_)
        ImGui::OpenPopup("Unsaved level");
    if (ImGui::BeginPopupModal("Unsaved level", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save your changes before continuing?");
        ImGui::TextDisabled("Your level has unsaved edits.");
        if (ImGui::Button("Save", {96, 0})) {
            unsaved_prompt_ = false;
            after_save_ = pending_action_;
            ImGui::CloseCurrentPopup();
            save_level();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", {96, 0})) {
            unsaved_prompt_ = false;
            const auto action = pending_action_;
            if (recovery_path_[0])
                SDL_RemovePath(recovery_path_.data());
            ImGui::CloseCurrentPopup();
            perform_action(action);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {96, 0})) {
            unsaved_prompt_ = false;
            pending_action_ = Action::none;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (recovery_available_)
        ImGui::OpenPopup("Recover level");
    if (ImGui::BeginPopupModal("Recover level", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("A recovery level is available from your last session.");
        ImGui::TextDisabled("Recovery opens as an unsaved level; choose Save As to keep it.");
        if (ImGui::Button("Restore")) {
            auto done = document_.recover(recovery_path_.data());
            if (!done)
                log(done.error().message);
            else
                log("Recovery level restored");
            recovery_available_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard recovery")) {
            SDL_RemovePath(recovery_path_.data());
            recovery_available_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Later")) {
            recovery_available_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (group_prompt_)
        ImGui::OpenPopup("Group actors");
    if (ImGui::BeginPopupModal("Group actors", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Group name", group_name_.data(), group_name_.size());
        if (ImGui::Button("Group")) {
            finish_edit();
            auto done = document_.group_selection(group_name_.data());
            if (!done)
                log(done.error().message);
            group_prompt_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            group_prompt_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    autosave_elapsed_ += ImGui::GetIO().DeltaTime;
    if (persistent_ && recovery_path_[0] && !recovery_available_ && autosave_elapsed_ >= 60 &&
        document_.dirty() && !document_.editing() && !playing_ && !files_.pending() && !unsaved_prompt_) {
        auto done = document_.autosave(recovery_path_.data());
        if (!done)
            log(done.error().message);
        else
            log("Recovery level updated");
        autosave_elapsed_ = 0;
    }
    if (main_window_) {
        const char *path = document_.path();
        const char *label = path[0] ? path : "Untitled";
        for (const char *at = path; *at; ++at)
            if (*at == '/' || *at == '\\')
                label = at + 1;
        char title[1200]{};
        std::snprintf(title, sizeof(title), "%s%s | Souls Editor", label, document_.dirty() ? " *" : "");
        SDL_SetWindowTitle(main_window_, title);
    }
}
} // namespace souls::editor
