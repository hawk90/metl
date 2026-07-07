#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/monotonic_buffer.hpp"

#include <cstddef>
#include <utility>

namespace metl {

/// @brief Typed, fixed-capacity allocator over an inline monotonic buffer.
///
/// Wraps a `monotonic_buffer` of @c Capacity bytes to hand out storage for @c T
/// objects; performs NO dynamic heap allocation. Storage is reclaimed only in bulk
/// via `reset`. Count-based requests use overflow-safe bounds checks.
///
/// @tparam T Element type this allocator produces storage for.
/// @tparam Capacity Backing storage size in bytes.
/// @warning NOT thread-safe: use from a single thread and never from an ISR.
template <typename T, std::size_t Capacity>
class static_allocator {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using size_type = std::size_t;

  /// @brief Construct an empty allocator with all storage available.
  constexpr static_allocator() noexcept = default;

  /// @brief Allocate uninitialized, aligned storage for @c count objects.
  /// @param count Number of contiguous @c T objects to reserve; 0 returns null.
  /// @return Pointer to the storage, or null on overflow or insufficient space.
  METL_NODISCARD pointer try_allocate(size_type count = 1) noexcept {
    if (count == 0) {
      return nullptr;
    }

    // Overflow-safe: reject before computing sizeof(T) * count so a huge count
    // cannot wrap size_type into a small byte request that slips past the
    // buffer's bounds check. sizeof(T) >= 1, so the division is well-defined.
    if (count > static_cast<size_type>(-1) / sizeof(T)) {
      return nullptr;
    }

    void* memory = buffer_.allocate(sizeof(T) * count, alignof(T));
    return static_cast<pointer>(memory);
  }

  /// @brief Like `try_allocate`, but asserts that the allocation succeeds.
  /// @param count Number of contiguous @c T objects to reserve.
  /// @return Pointer to the storage (never null on success).
  /// @pre There must be enough space for @c count objects.
  METL_NODISCARD pointer allocate(size_type count = 1) noexcept {
    pointer memory = try_allocate(count);
    METL_ASSERT(memory != nullptr);
    return memory;
  }

  /// @brief Allocate storage for one @c T and construct it in place.
  /// @return Pointer to the constructed object, or null if out of space.
  template <typename... Args>
  METL_NODISCARD pointer try_new(Args&&... args) {
    return buffer_.template try_emplace<T>(std::forward<Args>(args)...);
  }

  /// @brief Like `try_new`, but asserts that the allocation succeeds.
  /// @return Pointer to the constructed object (never null on success).
  /// @pre There must be enough space for one @c T.
  template <typename... Args>
  METL_NODISCARD pointer create(Args&&... args) {
    pointer object = try_new(std::forward<Args>(args)...);
    METL_ASSERT(object != nullptr);
    return object;
  }

  /// @brief Construct a @c T at previously allocated storage.
  /// @param location Destination storage (must be suitably sized and aligned).
  /// @param args Arguments forwarded to the @c T constructor.
  /// @pre @c location must be non-null.
  template <typename... Args>
  void construct(pointer location, Args&&... args) {
    METL_ASSERT(location != nullptr);
    new (location) T(std::forward<Args>(args)...);
  }

  /// @brief Run the destructor of a @c T (storage is not reclaimed).
  /// @param location Object to destroy.
  /// @pre @c location must be non-null and point to a live @c T.
  void destroy(pointer location) noexcept {
    METL_ASSERT(location != nullptr);
    location->~T();
  }

  /// @brief Release all storage at once (does NOT run object destructors).
  void reset() noexcept { buffer_.reset(); }

  /// @brief Total backing capacity in bytes.
  METL_NODISCARD constexpr size_type capacity_bytes() const noexcept { return Capacity; }
  /// @brief Bytes currently consumed, including alignment padding.
  METL_NODISCARD constexpr size_type used_bytes() const noexcept { return buffer_.used(); }
  /// @brief Bytes still available for allocation.
  METL_NODISCARD constexpr size_type remaining_bytes() const noexcept { return buffer_.remaining(); }
  /// @brief True if nothing has been allocated since construction or `reset`.
  METL_NODISCARD constexpr bool empty() const noexcept { return buffer_.empty(); }

 private:
  monotonic_buffer<Capacity> buffer_;
};

}  // namespace metl
