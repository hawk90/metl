#pragma once

/// @file
/// @brief Progress guarantees for `metl::static_allocator` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `try_allocate`, `allocate`, `try_new`, `create` | wait-free, bounded |
///   | `reset`, `used_bytes`, `remaining_bytes`, `capacity_bytes` | wait-free, bounded |
///
/// Allocation forwards to `metl::monotonic_buffer`: an alignment round-up and a
/// pointer bump, with no free list to walk and no search for a fit. A request that
/// does not fit is refused, not serviced more slowly.
///
/// There is no per-object deallocation, so there is no fragmentation and no path
/// whose cost grows with how long the allocator has been in use. `reset` releases
/// everything at once and does not run destructors.
///
/// Single-threaded: this type does not synchronise.

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
/// @deprecated Removed in 2.0 (#645): it is a `monotonic_buffer` with a type
///             attached. Use `monotonic_buffer<Capacity>` and its
///             `try_allocate_n<T>(count)` (the same overflow-safe count check),
///             `try_emplace<T>(args...)` and `reset()`.
template <typename T, std::size_t Capacity>
class METL_DEPRECATED("static_allocator is removed in 2.0; use monotonic_buffer and try_allocate_n<T> (#645)")
    static_allocator {
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
    // The same overflow-safe count check, now monotonic_buffer's own (#645).
    return buffer_.template try_allocate_n<T>(count);
  }

  /// @brief Like `try_allocate`, but asserts that the allocation succeeds.
  /// @param count Number of contiguous @c T objects to reserve.
  /// @return Pointer to the storage (never null on success).
  /// @pre There must be enough space for @c count objects, and @c count is not
  ///      0: `try_allocate(0)` returns null, so `allocate(0)` asserts.
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
    ::new (static_cast<void*>(location)) T(std::forward<Args>(args)...);
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
