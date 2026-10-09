#pragma once
#include <memory>
#include <souls/scene/Scene.hpp>

namespace souls::editor {
using ActorId = std::uint64_t;
enum class SelectionMode { replace, toggle, add };

// The document borrows the scene for its lifetime. UI adapters send edits here;
// rendering continues to read the same scene without knowing about undo or files.
class EditorDocument final {
  public:
    static constexpr std::size_t actor_limit = 256;
    static constexpr std::size_t history_limit = 64;
    EditorDocument() noexcept;
    ~EditorDocument();
    EditorDocument(EditorDocument &&) noexcept;
    EditorDocument &operator=(EditorDocument &&) noexcept;
    [[nodiscard]] static Result<EditorDocument> create(Scene &scene) noexcept;

    void select(EntityHandle entity, SelectionMode mode = SelectionMode::replace) noexcept;
    void clear_selection() noexcept;
    [[nodiscard]] std::span<const EntityHandle> selection() const noexcept;
    [[nodiscard]] EntityHandle primary() const noexcept;
    [[nodiscard]] ActorId actor_id(EntityHandle entity) const noexcept;
    [[nodiscard]] EntityHandle resolve(ActorId id) const noexcept;

    // A drag previews freely, then records one operation when the pointer is released.
    [[nodiscard]] Result<void> begin_edit(const char *label) noexcept;
    [[nodiscard]] Result<void> preview(EntityHandle entity, const Actor &actor) noexcept;
    [[nodiscard]] Result<void> commit_edit() noexcept;
    [[nodiscard]] Result<void> cancel_edit() noexcept;
    [[nodiscard]] bool editing() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] Result<EntityHandle> spawn(ActorKind kind, const char *label,
                                             Transform transform = {}) noexcept;
    [[nodiscard]] Result<void> duplicate_selection() noexcept;
    [[nodiscard]] Result<void> erase_selection() noexcept;
    [[nodiscard]] Result<void> group_selection(const char *label) noexcept;
    [[nodiscard]] Result<void> ungroup_selection() noexcept;
    [[nodiscard]] Result<void> set_selected_locked(bool locked) noexcept;
    [[nodiscard]] Result<void> undo() noexcept;
    [[nodiscard]] Result<void> redo() noexcept;
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    [[nodiscard]] const char *undo_label() const noexcept;
    [[nodiscard]] const char *redo_label() const noexcept;

    [[nodiscard]] Result<void> save(const char *path) noexcept;
    [[nodiscard]] Result<void> load(const char *path) noexcept;
    [[nodiscard]] Result<void> autosave(const char *path) noexcept;
    [[nodiscard]] Result<void> recover(const char *path) noexcept;
    [[nodiscard]] Result<void> new_scene(bool playground = true) noexcept;
    [[nodiscard]] const char *path() const noexcept;
    [[nodiscard]] Result<void> begin_play() noexcept;
    [[nodiscard]] Result<void> stop_play() noexcept;
    [[nodiscard]] bool playing() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace souls::editor
