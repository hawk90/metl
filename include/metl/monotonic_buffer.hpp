#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

/// @brief Fixed-capacity monotonic (bump-pointer) allocator with no per-object free.
///
/// Backed by an inline byte buffer of @c Capacity bytes; performs NO dynamic heap
/// allocation. Storage is only reclaimed all at once via `reset`; individual
/// allocations are never freed. Bounds checks are overflow-safe.
///
/// @tparam Capacity Backing storage size in bytes.
/// @warning NOT thread-safe: `allocate`/`reset` mutate a shared offset without
///          synchronization. Use from a single thread and never from an ISR.
template <std::size_t Capacity>
class monotonic_buffer {
 public:
  using size_type = std::size_t;

  /// @brief Construct an empty buffer with all storage available.
  constexpr monotonic_buffer() noexcept : offset_(0), storage_{} {}

  /// @brief Allocate a raw, uninitialized, aligned block by bumping the offset.
  /// @param bytes Number of bytes to allocate; a request of 0 returns null.
  /// @param alignment Required alignment; must not exceed max alignment.
  /// @return Pointer to the block, or null if the buffer lacks space (no throw).
  METL_NODISCARD void* allocate(size_type bytes, size_type alignment = alignof(std::max_align_t)) noexcept {
    if (bytes == 0) {
      return nullptr;
    }

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(&storage_[0]);
    const std::uintptr_t current = base + offset_;
    const std::uintptr_t aligned = align_up(current, alignment);
    const size_type padding = static_cast<size_type>(aligned - current);

    if (padding > remaining() || bytes > (remaining() - padding)) {
      return nullptr;
    }

    offset_ += padding;
    void* result = &storage_[offset_];
    offset_ += bytes;
    return result;
  }

  /// @brief Construct a @c T in the buffer (its destructor is NOT tracked or run).
  /// @tparam T Object type to construct; its alignment must not exceed max alignment.
  /// @return Pointer to the constructed object, or null if out of space.
  template <typename T, typename... Args>
  METL_NODISCARD T* try_emplace(Args&&... args) {
    static_assert(alignof(T) <= alignof(std::max_align_t),
                  "type alignment exceeds monotonic_buffer maximum alignment");

    void* memory = allocate(sizeof(T), alignof(T));
    if (memory == nullptr) {
      return nullptr;
    }

    return new (memory) T(std::forward<Args>(args)...);
  }

  /// @brief Like `try_emplace`, but asserts that the allocation succeeds.
  /// @tparam T Object type to construct.
  /// @return Pointer to the constructed object (never null on success).
  /// @pre The buffer must have enough space to hold a @c T.
  template <typename T, typename... Args>
  METL_NODISCARD T* emplace(Args&&... args) {
    T* object = try_emplace<T>(std::forward<Args>(args)...);
    METL_ASSERT(object != nullptr);
    return object;
  }

  /// @brief Release all storage at once (does NOT run any object destructors).
  void reset() noexcept { offset_ = 0; }

  /// @brief Total backing capacity in bytes.
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  /// @brief Bytes currently consumed, including alignment padding.
  METL_NODISCARD constexpr size_type used() const noexcept { return offset_; }
  /// @brief Bytes still available for allocation.
  METL_NODISCARD constexpr size_type remaining() const noexcept { return Capacity - offset_; }
  /// @brief True if nothing has been allocated since construction or `reset`.
  METL_NODISCARD constexpr bool empty() const noexcept { return offset_ == 0; }

 private:
  static constexpr std::uintptr_t align_up(std::uintptr_t value, size_type alignment) noexcept {
    return alignment == 0 ? value : (value + (alignment - 1)) & ~(alignment - 1);
  }

  size_type offset_;
  alignas(std::max_align_t) unsigned char storage_[Capacity == 0 ? 1 : Capacity];
};

}  // namespace metl
