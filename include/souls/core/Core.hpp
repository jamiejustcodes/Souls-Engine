#pragma once
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace souls {
enum class ErrorCode { exhausted, invalid_handle, invalid_argument, platform, gpu, unsupported, suspended };
struct Error { ErrorCode code; const char* message; std::int64_t native = 0; };
template<class T> using Result = std::expected<T, Error>;
template<class Tag> struct Handle final {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr std::uint64_t id() const noexcept { return (std::uint64_t{generation} << 32) | index; }
    friend constexpr bool operator==(Handle, Handle) = default;
};

// Borrowed storage. reset() is legal only after every consumer has finished.
class LinearArena final {
public:
    explicit LinearArena(std::span<std::byte> storage) noexcept : storage_(storage) {}
    [[nodiscard]] Result<std::span<std::byte>> allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) noexcept {
        if (!std::has_single_bit(alignment)) return std::unexpected(Error{ErrorCode::invalid_argument, "Alignment must be a power of two"});
        const auto address = reinterpret_cast<std::uintptr_t>(storage_.data()) + used_;
        const auto padding = (alignment - (address & (alignment - 1))) & (alignment - 1);
        const auto available = storage_.size() - used_;
        if (padding > available || bytes > available - padding) return std::unexpected(Error{ErrorCode::exhausted, "Frame arena exhausted"});
        used_ += padding;
        auto result = storage_.subspan(used_, bytes);
        used_ += bytes;
        if (used_ > high_water_) high_water_ = used_;
        return result;
    }
    void reset() noexcept { used_ = 0; }
    [[nodiscard]] std::size_t used() const noexcept { return used_; }
    [[nodiscard]] std::size_t high_water() const noexcept { return high_water_; }
private:
    std::span<std::byte> storage_;
    std::size_t used_ = 0, high_water_ = 0;
};

// Fixed storage, O(1) free list, generation-checked access. No allocation.
// Slots retire permanently on generation overflow: stale IDs never resurrect.
template<class T, class Tag, std::size_t Capacity> class Pool final {
    static_assert(Capacity > 0 && Capacity < std::numeric_limits<std::uint32_t>::max());
    struct Slot { std::optional<T> value; std::uint32_t generation = 1, next = 0; };
    static constexpr auto end = std::numeric_limits<std::uint32_t>::max();
public:
    Pool() noexcept { for (std::size_t i = 0; i < Capacity; ++i) slots_[i].next = i + 1 == Capacity ? end : static_cast<std::uint32_t>(i + 1); }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    template<class... Args> [[nodiscard]] Result<Handle<Tag>> emplace(Args&&... args) noexcept {
        static_assert(std::is_nothrow_constructible_v<T, Args...>);
        if (head_ == end) return std::unexpected(Error{ErrorCode::exhausted, "Pool exhausted"});
        const auto index = head_;
        auto& slot = slots_[index];
        head_ = slot.next;
        slot.value.emplace(std::forward<Args>(args)...);
        ++size_;
        return Handle<Tag>{index, slot.generation};
    }
    [[nodiscard]] T* get(Handle<Tag> handle) noexcept {
        if (handle.index >= Capacity || handle.generation == 0) return nullptr;
        auto& slot = slots_[handle.index];
        return slot.generation == handle.generation && slot.value ? &*slot.value : nullptr;
    }
    [[nodiscard]] const T* get(Handle<Tag> handle) const noexcept { return const_cast<Pool*>(this)->get(handle); }
    [[nodiscard]] Result<void> erase(Handle<Tag> handle) noexcept {
        if (!get(handle)) return std::unexpected(Error{ErrorCode::invalid_handle, "Stale or invalid handle"});
        auto& slot = slots_[handle.index];
        slot.value.reset();
        --size_;
        if (slot.generation != end) { ++slot.generation; slot.next = head_; head_ = handle.index; }
        return {};
    }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
private:
    std::array<Slot, Capacity> slots_{};
    std::uint32_t head_ = 0;
    std::size_t size_ = 0;
};
const char* version() noexcept;
} // namespace souls
