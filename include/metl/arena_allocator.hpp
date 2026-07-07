#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

/// @brief Fixed-capacity LIFO arena allocator with rewind and object destruction.
///
/// Backed by an inline byte buffer of @c Capacity bytes; performs NO dynamic heap
/// allocation. Each allocation records enough metadata to unwind in LIFO order via
/// `mark`/`rewind`, running destructors for objects created through `try_emplace`.
///
/// @tparam Capacity Backing storage size in bytes.
/// @warning NOT thread-safe: all operations mutate a shared offset without
///          synchronization. Use from a single thread and never from an ISR.
template <std::size_t Capacity>
class arena_allocator {
 public:
  using size_type = std::size_t;

  /// @brief Opaque savepoint capturing the arena offset for a later `rewind`.
  struct mark_type {
    size_type offset;
  };

  /// @brief Construct an empty arena with all storage available.
  constexpr arena_allocator() noexcept : offset_(0), storage_{} {}

  /// @brief Capture the current allocation position for a later `rewind`.
  /// @return A savepoint referring to the current top of the arena.
  METL_NODISCARD mark_type mark() const noexcept { return mark_type{offset_}; }

  /// @brief Allocate a raw, uninitialized, aligned block from the arena.
  ///
  /// Every successful allocation pushes a record so `rewind` can always walk back
  /// through allocations in LIFO order. Raw allocations carry no destructor, so
  /// rewind only unwinds offsets for them. This keeps rewind safe even when raw
  /// `allocate` is interleaved with `try_emplace`.
  ///
  /// @param bytes Number of bytes to allocate; a request of 0 returns null.
  /// @param alignment Required alignment; must not exceed max alignment.
  /// @return Pointer to the block, or null if the arena lacks space (no throw).
  METL_NODISCARD void* allocate(size_type bytes, size_type alignment = alignof(std::max_align_t)) noexcept {
    return allocate_impl(bytes, alignment, nullptr);
  }

  /// @brief Construct a @c T in the arena, registering its destructor for `rewind`.
  /// @tparam T Object type to construct; its alignment must not exceed max alignment.
  /// @return Pointer to the constructed object, or null if the arena is out of space.
  template <typename T, typename... Args>
  METL_NODISCARD T* try_emplace(Args&&... args) {
    static_assert(alignof(T) <= alignof(std::max_align_t),
                  "type alignment exceeds arena_allocator maximum alignment");

    void* memory = allocate_impl(sizeof(T), alignof(T), &destroy_object<T>);
    if (memory == nullptr) {
      return nullptr;
    }

    return new (memory) T(std::forward<Args>(args)...);
  }

  /// @brief Like `try_emplace`, but asserts that the allocation succeeds.
  /// @tparam T Object type to construct.
  /// @return Pointer to the constructed object (never null on success).
  /// @pre The arena must have enough space to hold a @c T.
  template <typename T, typename... Args>
  METL_NODISCARD T* emplace(Args&&... args) {
    T* object = try_emplace<T>(std::forward<Args>(args)...);
    METL_ASSERT(object != nullptr);
    return object;
  }

  /// @brief Roll the arena back to a savepoint, destroying objects created after it.
  ///
  /// Walks allocation records in LIFO order, running the destructor of each object
  /// created via `try_emplace`/`emplace` before releasing its space.
  ///
  /// @param target A savepoint previously returned by `mark`.
  /// @pre @c target must refer to a position at or below the current top.
  void rewind(mark_type target) noexcept {
    METL_ASSERT(target.offset <= offset_);
    while (offset_ > target.offset) {
      const allocation_record record = load_record(offset_);
      if (record.destroy != nullptr) {
        record.destroy(&storage_[record.payload_offset]);
      }
      offset_ = record.previous_offset;
    }
  }

  /// @brief Destroy all objects and release all storage back to empty.
  void reset() noexcept { rewind(mark_type{0}); }

  /// @brief Total backing capacity in bytes.
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  /// @brief Bytes currently consumed (payloads, padding, and records).
  METL_NODISCARD constexpr size_type used() const noexcept { return offset_; }
  /// @brief Bytes still available for allocation.
  METL_NODISCARD constexpr size_type remaining() const noexcept { return Capacity - offset_; }
  /// @brief True if no allocations are currently live.
  METL_NODISCARD constexpr bool empty() const noexcept { return offset_ == 0; }

 private:
  using destroy_fn_t = void (*)(void*) noexcept;

  struct allocation_record {
    size_type previous_offset;
    size_type payload_offset;
    destroy_fn_t destroy;
  };

  template <typename T>
  static void destroy_object(void* memory) noexcept {
    static_cast<T*>(memory)->~T();
  }

  static constexpr std::uintptr_t align_up(std::uintptr_t value, size_type alignment) noexcept {
    return alignment == 0 ? value : (value + (alignment - 1)) & ~(alignment - 1);
  }

  void* allocate_impl(size_type bytes, size_type alignment, destroy_fn_t destroy) noexcept {
    if (bytes == 0) {
      return nullptr;
    }

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(&storage_[0]);
    const std::uintptr_t current = base + offset_;
    const std::uintptr_t aligned = align_up(current, alignment);
    const size_type padding = static_cast<size_type>(aligned - current);

    // Overflow-safe bounds check. Computing `padding + bytes + sizeof(record)`
    // up front can wrap size_type for a huge `bytes`, yielding a small total
    // that slips past the bound and overruns the arena. Instead subtract each
    // component from the remaining space (which never underflows because each
    // step is guarded), so no addition can overflow before the check.
    const size_type space = remaining();
    if (padding > space) {
      return nullptr;
    }
    size_type left = space - padding;
    if (bytes > left) {
      return nullptr;
    }
    left -= bytes;
    if (sizeof(allocation_record) > left) {
      return nullptr;
    }

    // Safe now: padding + bytes + sizeof(record) <= space <= Capacity.
    const size_type total_bytes = padding + bytes + sizeof(allocation_record);

    const size_type previous_offset = offset_;
    const size_type payload_offset = previous_offset + padding;
    const size_type next_offset = previous_offset + total_bytes;

    store_record(next_offset, allocation_record{previous_offset, payload_offset, destroy});
    offset_ = next_offset;
    return &storage_[payload_offset];
  }

  void store_record(size_type end_offset, const allocation_record& record) noexcept {
    const unsigned char* source = reinterpret_cast<const unsigned char*>(&record);
    unsigned char* destination = &storage_[end_offset - sizeof(allocation_record)];
    for (size_type i = 0; i < sizeof(allocation_record); ++i) {
      destination[i] = source[i];
    }
  }

  allocation_record load_record(size_type end_offset) const noexcept {
    allocation_record record{};
    const unsigned char* source = &storage_[end_offset - sizeof(allocation_record)];
    unsigned char* destination = reinterpret_cast<unsigned char*>(&record);
    for (size_type i = 0; i < sizeof(allocation_record); ++i) {
      destination[i] = source[i];
    }
    return record;
  }

  size_type offset_;
  alignas(std::max_align_t) unsigned char storage_[Capacity == 0 ? 1 : Capacity];
};

}  // namespace metl
