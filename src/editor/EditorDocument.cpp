#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <souls/editor/EditorDocument.hpp>
#include <string_view>
#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace souls::editor {
namespace {
constexpr std::size_t capacity = EditorDocument::actor_limit;
struct Record {
    ActorId id = 0;
    Actor actor{};
};
struct Snapshot {
    std::array<Record, capacity> records{};
    std::array<ActorId, capacity> selected{};
    std::size_t count = 0, selection_count = 0;
};
struct Mapping {
    ActorId id = 0;
    EntityHandle handle{};
};
struct Operation {
    Snapshot before{}, after{};
    std::array<char, 64> label{};
    std::uint64_t before_revision = 0, after_revision = 0;
};
Result<void> fail(const char *message, ErrorCode code = ErrorCode::invalid_argument) noexcept {
    return std::unexpected(Error{code, message});
}
bool same(Vec3 a, Vec3 b) noexcept {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool same(const Actor &a, const Actor &b) noexcept {
    return a.label == b.label && a.group == b.group && a.locked == b.locked && a.kind == b.kind &&
           a.visible == b.visible && a.material == b.material && a.intensity == b.intensity &&
           a.attenuation == b.attenuation && same(a.color, b.color) && a.transform.x == b.transform.x &&
           a.transform.y == b.transform.y && a.transform.z == b.transform.z &&
           same(a.transform.rotation, b.transform.rotation) && same(a.transform.scale, b.transform.scale);
}
bool same_world(const Snapshot &a, const Snapshot &b) noexcept {
    if (a.count != b.count)
        return false;
    for (std::size_t i = 0; i < a.count; ++i) {
        if (a.records[i].id == b.records[i].id) {
            if (!same(a.records[i].actor, b.records[i].actor))
                return false;
            continue;
        }
        const auto found =
            std::find_if(b.records.begin(), b.records.begin() + static_cast<std::ptrdiff_t>(b.count),
                         [&](const Record &r) { return r.id == a.records[i].id; });
        if (found == b.records.begin() + static_cast<std::ptrdiff_t>(b.count) ||
            !same(a.records[i].actor, found->actor))
            return false;
    }
    return true;
}
bool valid_label(const std::array<char, 64> &label, bool allow_empty = false) noexcept {
    const auto end = std::find(label.begin(), label.end(), '\0');
    if (end == label.end())
        return false;
    if (end == label.begin())
        return allow_empty;
    const auto size = static_cast<std::size_t>(end - label.begin());
    for (std::size_t i = 0; i < size;) {
        const auto c = static_cast<unsigned char>(label[i]);
        if (c < 0x20 || c == 0x7f)
            return false;
        if (c < 0x80) {
            ++i;
            continue;
        }
        std::size_t n = c >= 0xf0 && c <= 0xf4   ? 4U
                        : c >= 0xe0 && c <= 0xef ? 3U
                        : c >= 0xc2 && c <= 0xdf ? 2U
                                                 : 0U;
        if (!n || i + n > size)
            return false;
        for (std::size_t j = 1; j < n; ++j)
            if ((static_cast<unsigned char>(label[i + j]) & 0xc0U) != 0x80U)
                return false;
        const auto next = static_cast<unsigned char>(label[i + 1]);
        if ((c == 0xe0 && next < 0xa0) || (c == 0xed && next >= 0xa0) || (c == 0xf0 && next < 0x90) ||
            (c == 0xf4 && next >= 0x90))
            return false;
        i += n;
    }
    return true;
}
bool valid_actor(const Actor &a) noexcept {
    const auto &t = a.transform;
    const float fields[]{t.x,          t.y,       t.z,         t.rotation.x, t.rotation.y,
                         t.rotation.z, t.scale.x, t.scale.y,   t.scale.z,    a.color.x,
                         a.color.y,    a.color.z, a.intensity, a.attenuation};
    for (float value : fields)
        if (!std::isfinite(value))
            return false;
    return valid_label(a.label) && valid_label(a.group, true) &&
           static_cast<std::uint32_t>(a.kind) < actor_kind_count && a.material <= 2 &&
           std::abs(t.scale.x) >= 0.001F && std::abs(t.scale.y) >= 0.001F && std::abs(t.scale.z) >= 0.001F &&
           a.color.x >= 0 && a.color.y >= 0 && a.color.z >= 0 && a.intensity >= 0 && a.attenuation >= 0;
}
#ifdef _WIN32
bool wide_path(const char *path, std::array<wchar_t, 1024> &out) noexcept {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out.data(),
                               static_cast<int>(out.size())) > 0;
}
#endif
FILE *open_file(const char *path, bool write) noexcept {
#ifdef _WIN32
    std::array<wchar_t, 1024> wide{};
    if (!wide_path(path, wide))
        return nullptr;
    FILE *file = nullptr;
    if (_wfopen_s(&file, wide.data(), write ? L"wb" : L"rb") != 0)
        return nullptr;
    return file;
#else
    return std::fopen(path, write ? "wb" : "rb");
#endif
}
bool replace_file(const char *temporary, const char *path) noexcept {
#ifdef _WIN32
    std::array<wchar_t, 1024> source{}, destination{};
    return wide_path(temporary, source) && wide_path(path, destination) &&
           MoveFileExW(source.data(), destination.data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    return std::rename(temporary, path) == 0;
#endif
}
void remove_file(const char *path) noexcept {
#ifdef _WIN32
    std::array<wchar_t, 1024> wide{};
    if (wide_path(path, wide))
        _wremove(wide.data());
#else
    std::remove(path);
#endif
}
struct Line {
    std::array<char, 2048> text{};
    std::size_t size = 0;
    bool append(std::string_view value) noexcept {
        if (value.size() > text.size() - size)
            return false;
        std::memcpy(text.data() + size, value.data(), value.size());
        size += value.size();
        return true;
    }
    template <class T> bool number(T value) noexcept {
        if (!append(" "))
            return false;
        auto converted = std::to_chars(text.data() + size, text.data() + text.size(), value);
        if (converted.ec != std::errc{})
            return false;
        size = static_cast<std::size_t>(converted.ptr - text.data());
        return true;
    }
    bool write(FILE *file) noexcept {
        return append("\n") && std::fwrite(text.data(), 1, size, file) == size;
    }
};
Result<void> write_snapshot(const char *path, const Snapshot &snapshot) noexcept {
    if (!path || !*path || std::strlen(path) > 1000)
        return fail("Choose a valid level path");
    for (std::size_t i = 0; i < snapshot.count; ++i)
        if (!snapshot.records[i].id || snapshot.records[i].id == std::numeric_limits<ActorId>::max() ||
            !valid_actor(snapshot.records[i].actor))
            return fail("The document contains an invalid actor");
    std::array<char, 1024> temporary{};
    std::snprintf(temporary.data(), temporary.size(), "%s.tmp", path);
    FILE *file = open_file(temporary.data(), true);
    if (!file)
        return fail("Cannot open the temporary level file", ErrorCode::platform);
    bool okay = std::fputs("SOULS_LEVEL 1\n", file) >= 0;
    Line count;
    okay = okay && count.append("COUNT") && count.number(snapshot.count) && count.write(file);
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; okay && i < snapshot.count; ++i) {
        const auto &r = snapshot.records[i];
        const auto &a = r.actor;
        const auto &t = a.transform;
        Line line;
        okay = line.append("ACTOR") && line.number(r.id) && line.number(static_cast<std::uint32_t>(a.kind)) &&
               line.number(a.visible ? 1 : 0) && line.number(a.locked ? 1 : 0) && line.number(a.material);
        for (float f : {t.x, t.y, t.z, t.rotation.x, t.rotation.y, t.rotation.z, t.scale.x, t.scale.y,
                        t.scale.z, a.color.x, a.color.y, a.color.z, a.intensity, a.attenuation})
            okay = okay && line.number(f);
        okay = okay && line.append(" ");
        // Hex labels keep whitespace, Unicode, and punctuation unambiguous without locale-sensitive parsing.
        for (char c : a.label) {
            if (!c)
                break;
            const auto byte = static_cast<unsigned char>(c);
            const char encoded[]{hex[byte >> 4], hex[byte & 15U]};
            okay = okay && line.append(std::string_view{encoded, 2});
        }
        okay = okay && line.append(" ");
        if (!a.group[0])
            okay = okay && line.append("-");
        else
            for (char c : a.group) {
                if (!c)
                    break;
                const auto byte = static_cast<unsigned char>(c);
                const char encoded[]{hex[byte >> 4], hex[byte & 15U]};
                okay = okay && line.append(std::string_view{encoded, 2});
            }
        okay = okay && line.write(file);
    }
    okay = okay && std::fputs("END\n", file) >= 0 && std::fflush(file) == 0;
#ifdef _WIN32
    if (okay)
        okay = _commit(_fileno(file)) == 0;
#else
    if (okay)
        okay = fsync(fileno(file)) == 0;
#endif
    if (std::fclose(file) != 0)
        okay = false;
    if (!okay || !replace_file(temporary.data(), path)) {
        remove_file(temporary.data());
        return fail("Level save failed; the previous file was preserved", ErrorCode::platform);
    }
    return {};
}
struct Tokens {
    std::string_view text;
    std::string_view next() noexcept {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            text = {};
            return {};
        }
        text.remove_prefix(first);
        const auto end = text.find_first_of(" \t\r\n");
        auto result = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end);
        return result;
    }
    template <class T> bool number(T &value) noexcept {
        const auto token = next();
        if (token.empty())
            return false;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size();
    }
};
bool read_line(FILE *file, std::array<char, 2048> &buffer) noexcept {
    buffer.fill(0);
    std::size_t length = 0;
    for (;;) {
        const int c = std::fgetc(file);
        if (c == EOF)
            return length > 0 && !std::ferror(file);
        if (c == 0 || length + 1 >= buffer.size())
            return false;
        if (c == '\n')
            return true;
        buffer[length++] = static_cast<char>(c);
    }
}
Result<Snapshot> read_snapshot(const char *path) noexcept {
    if (!path || !*path || std::strlen(path) > 1000)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Choose a valid level path"});
    FILE *file = open_file(path, false);
    if (!file)
        return std::unexpected(Error{ErrorCode::platform, "Cannot open level file"});
    Snapshot snapshot;
    std::array<char, 2048> line{};
    bool okay = read_line(file, line);
    Tokens header{line.data()};
    std::uint32_t version = 0;
    okay = okay && header.next() == "SOULS_LEVEL" && header.number(version) && version == 1 &&
           header.next().empty();
    okay = okay && read_line(file, line);
    Tokens count{line.data()};
    okay = okay && count.next() == "COUNT" && count.number(snapshot.count) && snapshot.count <= capacity &&
           count.next().empty();
    for (std::size_t i = 0; okay && i < snapshot.count; ++i) {
        okay = read_line(file, line);
        Tokens fields{line.data()};
        auto &r = snapshot.records[i];
        auto &a = r.actor;
        auto &t = a.transform;
        std::uint32_t kind = 0, visible = 0, locked = 0;
        okay = okay && fields.next() == "ACTOR" && fields.number(r.id) && r.id != 0 &&
               r.id != std::numeric_limits<ActorId>::max() && fields.number(kind) && fields.number(visible) &&
               visible <= 1 && fields.number(locked) && locked <= 1 && fields.number(a.material);
        a.kind = static_cast<ActorKind>(kind);
        a.visible = visible != 0;
        a.locked = locked != 0;
        float *numbers[]{&t.x,          &t.y,       &t.z,         &t.rotation.x, &t.rotation.y,
                         &t.rotation.z, &t.scale.x, &t.scale.y,   &t.scale.z,    &a.color.x,
                         &a.color.y,    &a.color.z, &a.intensity, &a.attenuation};
        for (float *f : numbers)
            okay = okay && fields.number(*f);
        const auto label = fields.next();
        okay = okay && !label.empty() && label.size() <= 126 && label.size() % 2 == 0;
        a.label.fill(0);
        for (std::size_t j = 0; okay && j < label.size() / 2; ++j) {
            unsigned byte = 0;
            auto decoded = std::from_chars(label.data() + j * 2, label.data() + j * 2 + 2, byte, 16);
            okay = decoded.ec == std::errc{} && decoded.ptr == label.data() + j * 2 + 2 && byte > 0;
            a.label[j] = static_cast<char>(byte);
        }
        const auto group = fields.next();
        okay = okay && !group.empty() && fields.next().empty();
        a.group.fill(0);
        if (group != "-") {
            okay = okay && group.size() <= 126 && group.size() % 2 == 0;
            for (std::size_t j = 0; okay && j < group.size() / 2; ++j) {
                unsigned byte = 0;
                auto decoded = std::from_chars(group.data() + j * 2, group.data() + j * 2 + 2, byte, 16);
                okay = decoded.ec == std::errc{} && decoded.ptr == group.data() + j * 2 + 2 && byte > 0;
                a.group[j] = static_cast<char>(byte);
            }
        }
        okay = okay && valid_actor(a);
        for (std::size_t j = 0; j < i; ++j)
            if (snapshot.records[j].id == r.id)
                okay = false;
    }
    okay = okay && read_line(file, line);
    Tokens ending{line.data()};
    okay = okay && ending.next() == "END" && ending.next().empty();
    if (okay)
        for (int c = std::fgetc(file); c != EOF; c = std::fgetc(file)) {
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                okay = false;
                break;
            }
        }
    if (std::ferror(file))
        okay = false;
    std::fclose(file);
    if (!okay)
        return std::unexpected(
            Error{ErrorCode::invalid_argument, "Malformed or unsupported Souls level file"});
    return snapshot;
}
} // namespace

struct EditorDocument::Impl {
    Scene *scene = nullptr;
    std::array<Mapping, capacity> mapping{};
    std::size_t count = 0;
    std::array<EntityHandle, capacity> selected{};
    std::size_t selection_count = 0;
    // Allocate the fixed history array once. Keeping it separate also avoids
    // expanding thousands of nested aggregate initializers in MSVC's ASan build.
    std::unique_ptr<Operation[]> history;
    std::size_t history_start = 0, history_count = 0, history_cursor = 0;
    Snapshot before{}, play_snapshot{};
    std::array<char, 64> edit_label{};
    std::array<char, 1024> file_path{};
    std::uint64_t revision = 1, next_revision = 2, saved_revision = 1;
    ActorId next_id = 1;
    bool editing = false, pending = false, playing = false, structural_pending = false;
    std::array<bool, capacity> changed_records{};
    ActorId id(EntityHandle handle) const noexcept {
        for (std::size_t i = 0; i < count; ++i)
            if (mapping[i].handle == handle)
                return mapping[i].id;
        return 0;
    }
    EntityHandle handle(ActorId actor) const noexcept {
        for (std::size_t i = 0; i < count; ++i)
            if (mapping[i].id == actor)
                return mapping[i].handle;
        return {};
    }
    Snapshot capture() const noexcept {
        Snapshot result;
        result.count = count;
        for (std::size_t i = 0; i < count; ++i) {
            result.records[i].id = mapping[i].id;
            const auto a = scene->actor(mapping[i].handle);
            assert(a);
            if (a)
                result.records[i].actor = *a;
        }
        result.selection_count = selection_count;
        for (std::size_t i = 0; i < selection_count; ++i)
            result.selected[i] = id(selected[i]);
        return result;
    }
    void restore_selection(const Snapshot &snapshot) noexcept {
        selection_count = 0;
        for (std::size_t i = 0; i < snapshot.selection_count; ++i)
            if (auto h = handle(snapshot.selected[i]); h)
                selected[selection_count++] = h;
    }
    Result<void> restore(const Snapshot &snapshot) noexcept {
        std::array<Mapping, capacity> next{};
        std::array<EntityHandle, capacity> created{};
        std::size_t created_count = 0;
        // Create missing actors before touching existing ones. Failure can then roll back
        // the new actors without losing the user's original selection or edit world.
        for (std::size_t i = 0; i < snapshot.count; ++i) {
            const auto &r = snapshot.records[i];
            auto h = handle(r.id);
            if (!h || !scene->actor(h)) {
                auto made = scene->spawn(r.actor.kind, r.actor.label.data(), r.actor.transform);
                if (!made) {
                    for (std::size_t j = 0; j < created_count; ++j) {
                        const auto ignored = scene->remove(created[j]);
                        (void)ignored;
                    }
                    return std::unexpected(made.error());
                }
                h = *made;
                created[created_count++] = h;
            }
            next[i] = {r.id, h};
        }
        for (std::size_t i = 0; i < snapshot.count; ++i) {
            auto done = scene->update(next[i].handle, snapshot.records[i].actor);
            if (!done)
                return done;
        }
        for (std::size_t i = 0; i < count; ++i) {
            bool keep = false;
            for (std::size_t j = 0; j < snapshot.count; ++j)
                if (mapping[i].id == next[j].id)
                    keep = true;
            if (!keep && scene->actor(mapping[i].handle)) {
                auto done = scene->remove(mapping[i].handle);
                if (!done)
                    return done;
            }
        }
        mapping = next;
        count = snapshot.count;
        restore_selection(snapshot);
        return {};
    }
    Result<void> replace(const Snapshot &snapshot) noexcept {
        auto candidate = Scene::create();
        if (!candidate)
            return std::unexpected(candidate.error());
        std::array<Mapping, capacity> next{};
        for (std::size_t i = 0; i < snapshot.count; ++i) {
            const auto &r = snapshot.records[i];
            auto h = candidate->spawn(r.actor.kind, r.actor.label.data(), r.actor.transform);
            if (!h)
                return std::unexpected(h.error());
            auto done = candidate->update(*h, r.actor);
            if (!done)
                return done;
            next[i] = {r.id, *h};
        }
        // The scene object itself stays at the same address; render adapters keep their reference.
        *scene = std::move(*candidate);
        mapping = next;
        count = snapshot.count;
        restore_selection(snapshot);
        return {};
    }
    void reset_history(bool clean) noexcept {
        history_start = history_count = history_cursor = 0;
        revision = next_revision++;
        saved_revision = clean ? revision : 0;
        editing = pending = false;
    }
    void refresh_pending() noexcept {
        if (!editing)
            return;
        const auto current = capture();
        structural_pending = before.count != current.count;
        changed_records.fill(false);
        for (std::size_t i = 0; i < before.count; ++i) {
            bool found = false;
            for (std::size_t j = 0; j < current.count; ++j)
                if (before.records[i].id == current.records[j].id) {
                    found = true;
                    changed_records[i] = !same(before.records[i].actor, current.records[j].actor);
                    break;
                }
            if (!found)
                structural_pending = true;
        }
        pending = structural_pending ||
                  std::any_of(changed_records.begin(), changed_records.end(), [](bool c) { return c; });
    }
    void refresh_preview(EntityHandle entity, const Actor &actor) noexcept {
        const auto stable_id = id(entity);
        for (std::size_t i = 0; i < before.count; ++i)
            if (before.records[i].id == stable_id) {
                changed_records[i] = !same(before.records[i].actor, actor);
                break;
            }
        pending = structural_pending ||
                  std::any_of(changed_records.begin(), changed_records.end(), [](bool c) { return c; });
    }
    Operation &operation(std::size_t logical) noexcept {
        return history[(history_start + logical) % history_limit];
    }
    const Operation &operation(std::size_t logical) const noexcept {
        return history[(history_start + logical) % history_limit];
    }
};

EditorDocument::EditorDocument() noexcept = default;
EditorDocument::~EditorDocument() = default;
EditorDocument::EditorDocument(EditorDocument &&) noexcept = default;
EditorDocument &EditorDocument::operator=(EditorDocument &&) noexcept = default;
Result<EditorDocument> EditorDocument::create(Scene &scene) noexcept {
    if (scene.size() > actor_limit)
        return std::unexpected(Error{ErrorCode::exhausted, "Editor supports 256 actors"});
    EditorDocument result;
    result.impl_.reset(new (std::nothrow) Impl);
    if (!result.impl_)
        return std::unexpected(Error{ErrorCode::exhausted, "Editor document allocation failed"});
    auto &p = *result.impl_;
    p.history.reset(new (std::nothrow) Operation[history_limit]);
    if (!p.history)
        return std::unexpected(Error{ErrorCode::exhausted, "Editor history allocation failed"});
    p.scene = &scene;
    for (auto h : scene.actors())
        p.mapping[p.count++] = {p.next_id++, h};
    return result;
}
void EditorDocument::select(EntityHandle entity, SelectionMode mode) noexcept {
    auto &p = *impl_;
    if (!entity || !p.id(entity) || !p.scene->actor(entity)) {
        if (mode == SelectionMode::replace)
            clear_selection();
        return;
    }
    auto end = p.selected.begin() + static_cast<std::ptrdiff_t>(p.selection_count);
    auto found = std::find(p.selected.begin(), end, entity);
    if (mode == SelectionMode::replace) {
        p.selected[0] = entity;
        p.selection_count = 1;
    } else if (found == end)
        p.selected[p.selection_count++] = entity;
    else if (mode == SelectionMode::toggle) {
        std::move(found + 1, end, found);
        --p.selection_count;
    }
}
void EditorDocument::clear_selection() noexcept {
    impl_->selection_count = 0;
}
std::span<const EntityHandle> EditorDocument::selection() const noexcept {
    return {impl_->selected.data(), impl_->selection_count};
}
EntityHandle EditorDocument::primary() const noexcept {
    return impl_->selection_count ? impl_->selected[impl_->selection_count - 1] : EntityHandle{};
}
ActorId EditorDocument::actor_id(EntityHandle entity) const noexcept {
    return impl_->id(entity);
}
EntityHandle EditorDocument::resolve(ActorId id) const noexcept {
    return impl_->handle(id);
}
bool EditorDocument::editing() const noexcept {
    return impl_->editing;
}
bool EditorDocument::dirty() const noexcept {
    return impl_->revision != impl_->saved_revision || (!impl_->playing && impl_->pending);
}
Result<void> EditorDocument::begin_edit(const char *label) noexcept {
    auto &p = *impl_;
    if (p.editing)
        return fail("Finish the current edit first");
    if (!label || !*label)
        return fail("An edit needs a descriptive label");
    p.before = p.capture();
    p.editing = true;
    p.pending = p.structural_pending = false;
    p.changed_records.fill(false);
    std::snprintf(p.edit_label.data(), p.edit_label.size(), "%s", label);
    return {};
}
Result<void> EditorDocument::preview(EntityHandle entity, const Actor &actor) noexcept {
    auto &p = *impl_;
    if (!p.editing)
        return fail("Begin an edit before changing an actor");
    if (!p.id(entity))
        return fail("Actor does not belong to this document", ErrorCode::invalid_handle);
    if (!valid_actor(actor))
        return fail("Actor contains an invalid label, transform, or material");
    const auto original = p.scene->actor(entity);
    if (!original)
        return std::unexpected(original.error());
    const auto &a = original->transform;
    const auto &b = actor.transform;
    if (original->locked &&
        (a.x != b.x || a.y != b.y || a.z != b.z || !same(a.rotation, b.rotation) || !same(a.scale, b.scale)))
        return fail("Unlock the actor before moving it");
    auto done = p.scene->update(entity, actor);
    if (done)
        p.refresh_preview(entity, actor);
    return done;
}
Result<void> EditorDocument::commit_edit() noexcept {
    auto &p = *impl_;
    if (!p.editing)
        return fail("No edit is active");
    auto after = p.capture();
    if (!p.playing && !same_world(p.before, after)) {
        p.history_count = p.history_cursor;
        if (p.history_count == history_limit) {
            p.history_start = (p.history_start + 1) % history_limit;
            --p.history_count;
            --p.history_cursor;
        }
        auto &entry = p.operation(p.history_count++);
        entry.before = p.before;
        entry.after = after;
        entry.label = p.edit_label;
        entry.before_revision = p.revision;
        entry.after_revision = p.next_revision++;
        p.revision = entry.after_revision;
        p.history_cursor = p.history_count;
    }
    p.editing = p.pending = false;
    return {};
}
Result<void> EditorDocument::cancel_edit() noexcept {
    auto &p = *impl_;
    if (!p.editing)
        return fail("No edit is active");
    auto done = p.restore(p.before);
    if (done)
        p.editing = p.pending = false;
    return done;
}
Result<EntityHandle> EditorDocument::spawn(ActorKind kind, const char *label, Transform transform) noexcept {
    auto &p = *impl_;
    if (p.count == capacity)
        return std::unexpected(Error{ErrorCode::exhausted, "Editor supports 256 actors"});
    if (!label || std::strlen(label) > 63)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Actor labels must fit in 63 UTF-8 bytes"});
    if (!p.next_id || p.next_id == std::numeric_limits<ActorId>::max())
        return std::unexpected(Error{ErrorCode::exhausted, "Document actor IDs are exhausted"});
    const bool implicit = !p.editing;
    if (implicit) {
        auto begun = begin_edit("Add actor");
        if (!begun)
            return std::unexpected(begun.error());
    }
    auto h = p.scene->spawn(kind, label, transform);
    if (!h) {
        if (implicit) {
            const auto ignored = cancel_edit();
            (void)ignored;
        }
        return std::unexpected(h.error());
    }
    auto actor = p.scene->actor(*h);
    if (!actor || !valid_actor(*actor)) {
        const auto ignored = p.scene->remove(*h);
        (void)ignored;
        if (implicit) {
            const auto cancelled = cancel_edit();
            (void)cancelled;
        }
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid actor label or transform"});
    }
    p.mapping[p.count++] = {p.next_id++, *h};
    select(*h);
    p.refresh_pending();
    if (implicit) {
        auto done = commit_edit();
        if (!done)
            return std::unexpected(done.error());
    }
    return *h;
}
Result<void> EditorDocument::duplicate_selection() noexcept {
    auto &p = *impl_;
    if (!p.selection_count)
        return fail("Select actors to duplicate");
    if (p.count + p.selection_count > capacity)
        return fail("Duplicating exceeds the 256 actor limit", ErrorCode::exhausted);
    const auto original = p.capture();
    const bool implicit = !p.editing;
    if (implicit) {
        auto begun = begin_edit("Duplicate actors");
        if (!begun)
            return begun;
    }
    std::array<EntityHandle, capacity> copies{};
    const auto selected = p.selected;
    const auto count = p.selection_count;
    for (std::size_t i = 0; i < count; ++i) {
        auto a = p.scene->actor(selected[i]);
        if (!a)
            return std::unexpected(a.error());
        a->transform.x += 2;
        a->locked = false;
        std::array<char, 64> name{};
        std::array<char, 32> suffix{};
        std::snprintf(suffix.data(), suffix.size(), " Copy %llu", static_cast<unsigned long long>(p.next_id));
        auto prefix = std::min(std::strlen(a->label.data()), name.size() - 1 - std::strlen(suffix.data()));
        std::memcpy(name.data(), a->label.data(), prefix);
        // A byte limit may land in the middle of a UTF-8 character. Trim back to
        // a complete prefix before appending the duplicate's unique suffix.
        while (prefix && !valid_label(name))
            name[--prefix] = 0;
        std::memcpy(name.data() + prefix, suffix.data(), std::strlen(suffix.data()) + 1);
        auto copy = spawn(a->kind, name.data(), a->transform);
        if (!copy) {
            auto rollback = p.restore(original);
            (void)rollback;
            if (implicit)
                p.editing = p.pending = false;
            return std::unexpected(copy.error());
        }
        a->label = name;
        auto done = preview(*copy, *a);
        if (!done)
            return done;
        copies[i] = *copy;
    }
    p.selected = copies;
    p.selection_count = count;
    p.refresh_pending();
    return implicit ? commit_edit() : Result<void>{};
}
Result<void> EditorDocument::erase_selection() noexcept {
    auto &p = *impl_;
    if (!p.selection_count)
        return fail("Select actors to delete");
    for (auto h : selection()) {
        auto a = p.scene->actor(h);
        if (!a || a->locked)
            return fail("Unlock selected actors before deleting them");
    }
    const bool implicit = !p.editing;
    if (implicit) {
        auto begun = begin_edit("Delete actors");
        if (!begun)
            return begun;
    }
    for (std::size_t i = 0; i < p.selection_count; ++i) {
        const auto h = p.selected[i];
        auto removed = p.scene->remove(h);
        if (!removed)
            return removed;
        for (std::size_t j = 0; j < p.count; ++j)
            if (p.mapping[j].handle == h) {
                std::move(p.mapping.begin() + static_cast<std::ptrdiff_t>(j + 1),
                          p.mapping.begin() + static_cast<std::ptrdiff_t>(p.count),
                          p.mapping.begin() + static_cast<std::ptrdiff_t>(j));
                --p.count;
                break;
            }
    }
    clear_selection();
    p.refresh_pending();
    return implicit ? commit_edit() : Result<void>{};
}
Result<void> EditorDocument::group_selection(const char *label) noexcept {
    auto &p = *impl_;
    if (!label || std::strlen(label) > 63 || !p.selection_count)
        return fail("Select actors and enter a group name");
    std::array<char, 64> group{};
    std::snprintf(group.data(), group.size(), "%s", label);
    if (!valid_label(group, true))
        return fail("Invalid group name");
    for (auto h : selection()) {
        auto a = p.scene->actor(h);
        if (!a || a->locked)
            return fail("Unlock selected actors before grouping them");
    }
    const bool implicit = !p.editing;
    if (implicit) {
        auto begun = begin_edit(*label ? "Group actors" : "Ungroup actors");
        if (!begun)
            return begun;
    }
    for (auto h : selection()) {
        auto a = p.scene->actor(h);
        if (!a)
            return std::unexpected(a.error());
        a->group = group;
        auto done = preview(h, *a);
        if (!done)
            return done;
    }
    return implicit ? commit_edit() : Result<void>{};
}
Result<void> EditorDocument::ungroup_selection() noexcept {
    return group_selection("");
}
Result<void> EditorDocument::set_selected_locked(bool locked) noexcept {
    auto &p = *impl_;
    if (!p.selection_count)
        return fail("Select actors to lock or unlock");
    const bool implicit = !p.editing;
    if (implicit) {
        auto begun = begin_edit(locked ? "Lock actors" : "Unlock actors");
        if (!begun)
            return begun;
    }
    for (auto h : selection()) {
        auto a = p.scene->actor(h);
        if (!a)
            return std::unexpected(a.error());
        a->locked = locked;
        auto done = preview(h, *a);
        if (!done)
            return done;
    }
    return implicit ? commit_edit() : Result<void>{};
}
bool EditorDocument::can_undo() const noexcept {
    return !impl_->playing && !impl_->editing && impl_->history_cursor > 0;
}
bool EditorDocument::can_redo() const noexcept {
    return !impl_->playing && !impl_->editing && impl_->history_cursor < impl_->history_count;
}
const char *EditorDocument::undo_label() const noexcept {
    return can_undo() ? impl_->operation(impl_->history_cursor - 1).label.data() : "";
}
const char *EditorDocument::redo_label() const noexcept {
    return can_redo() ? impl_->operation(impl_->history_cursor).label.data() : "";
}
Result<void> EditorDocument::undo() noexcept {
    if (!can_undo())
        return fail("No edit can be undone");
    auto &p = *impl_;
    const auto &entry = p.operation(p.history_cursor - 1);
    auto done = p.restore(entry.before);
    if (done) {
        --p.history_cursor;
        p.revision = entry.before_revision;
    }
    return done;
}
Result<void> EditorDocument::redo() noexcept {
    if (!can_redo())
        return fail("No edit can be redone");
    auto &p = *impl_;
    const auto &entry = p.operation(p.history_cursor);
    auto done = p.restore(entry.after);
    if (done) {
        ++p.history_cursor;
        p.revision = entry.after_revision;
    }
    return done;
}
Result<void> EditorDocument::save(const char *path) noexcept {
    auto &p = *impl_;
    if (p.playing || p.editing)
        return fail("Finish editing or stop simulation before saving");
    auto done = write_snapshot(path, p.capture());
    if (done) {
        std::memmove(p.file_path.data(), path, std::strlen(path) + 1);
        p.saved_revision = p.revision;
    }
    return done;
}
Result<void> EditorDocument::autosave(const char *path) noexcept {
    auto &p = *impl_;
    if (p.playing || p.editing)
        return fail("Autosave waits until the current edit is finished");
    return write_snapshot(path, p.capture());
}
Result<void> EditorDocument::load(const char *path) noexcept {
    auto &p = *impl_;
    if (p.playing || p.editing)
        return fail("Finish editing or stop simulation before loading");
    auto snapshot = read_snapshot(path);
    if (!snapshot)
        return std::unexpected(snapshot.error());
    auto done = p.replace(*snapshot);
    if (!done)
        return done;
    p.next_id = 1;
    for (std::size_t i = 0; i < snapshot->count; ++i)
        p.next_id = std::max(p.next_id, snapshot->records[i].id + 1);
    std::memmove(p.file_path.data(), path, std::strlen(path) + 1);
    p.reset_history(true);
    return {};
}
Result<void> EditorDocument::recover(const char *path) noexcept {
    auto done = load(path);
    if (done) {
        impl_->file_path.fill(0);
        impl_->saved_revision = 0;
    }
    return done;
}
Result<void> EditorDocument::new_scene(bool playground) noexcept {
    auto &p = *impl_;
    if (p.playing || p.editing)
        return fail("Finish editing or stop simulation before creating a level");
    auto candidate = Scene::create();
    if (!candidate)
        return std::unexpected(candidate.error());
    if (playground) {
        auto done = candidate->playground();
        if (!done)
            return done;
    }
    if (candidate->size() > std::numeric_limits<ActorId>::max() - p.next_id)
        return fail("Document actor IDs are exhausted", ErrorCode::exhausted);
    std::array<Mapping, capacity> mapping{};
    std::size_t count = 0;
    for (auto h : candidate->actors())
        mapping[count++] = {p.next_id++, h};
    *p.scene = std::move(*candidate);
    p.mapping = mapping;
    p.count = count;
    clear_selection();
    p.file_path.fill(0);
    p.reset_history(false);
    return {};
}
Result<void> EditorDocument::replace_scene(Scene &&candidate) noexcept {
    auto &p = *impl_;
    if (p.playing || p.editing || &candidate == p.scene)
        return fail("Finish editing before replacing the level");
    if (candidate.size() > actor_limit || candidate.size() > std::numeric_limits<ActorId>::max() - p.next_id)
        return fail("The template exceeds document capacity", ErrorCode::exhausted);
    std::array<Mapping, capacity> mapping{};
    std::size_t count = 0;
    auto next_id = p.next_id;
    for (auto h : candidate.actors()) {
        auto actor = candidate.actor(h);
        if (!actor || !valid_actor(*actor))
            return fail("The template contains an invalid actor");
        mapping[count++] = {next_id++, h};
    }
    *p.scene = std::move(candidate);
    p.mapping = mapping;
    p.count = count;
    p.next_id = next_id;
    p.file_path.fill(0);
    clear_selection();
    p.reset_history(false);
    return {};
}
const char *EditorDocument::path() const noexcept {
    return impl_->file_path.data();
}
Result<void> EditorDocument::begin_play() noexcept {
    auto &p = *impl_;
    if (p.playing || p.editing)
        return fail("Finish the current edit before starting simulation");
    p.play_snapshot = p.capture();
    p.playing = true;
    return {};
}
Result<void> EditorDocument::stop_play() noexcept {
    auto &p = *impl_;
    if (!p.playing)
        return fail("Simulation is not running");
    auto done = p.restore(p.play_snapshot);
    if (done) {
        p.playing = p.editing = p.pending = false;
    }
    return done;
}
bool EditorDocument::playing() const noexcept {
    return impl_->playing;
}
} // namespace souls::editor
