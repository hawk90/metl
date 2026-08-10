#pragma once

#include "metl/assert.hpp"
#include "metl/attributes.hpp"
#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/type_traits.hpp"
#include "metl/versioned_handle.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace metl {

/// @brief Slot-based object pool addressed by generation-tagged handles.
///
/// Same fixed-capacity, zero-heap model as `object_pool`, with two differences
/// that follow from replacing the raw pointer with a `versioned_handle`:
///
///   * **Use-after-free is detected, not undefined.** Every slot carries a
///     generation counter that is bumped on destroy, so a handle to a destroyed
///     object no longer matches its slot and `get()` returns `nullptr` instead
///     of a dangling pointer into a recycled slot. This also gives a weak
///     reference for free: hold the handle, check on each use.
///   * **Allocation is O(1), not O(Capacity).** Free slots form an intrusive
///     index free-list, where `object_pool::try_emplace` linearly scans for the
///     first inactive slot.
///
/// The handle is 32 bits by default (16-bit index + 16-bit generation) and
/// makes no assumption whatsoever about pointer representation, which is what
/// makes it portable to targets where the spare bits of a pointer belong to the
/// hardware (AArch64 PAC/MTE, x86-64 LA57/LAM). See versioned_handle.hpp.
///
/// Progress guarantees:
///
///   | Operation                   | Guarantee            |
///   |-----------------------------|----------------------|
///   | `try_emplace` / `destroy`   | wait-free, bounded   |
///   | `get` / `contains`          | wait-free, bounded   |
///   | constructor / `clear`       | bounded, O(Capacity) |
///
/// @tparam T Pooled object type.
/// @tparam Capacity Number of slots (fixed at compile time).
/// @tparam GenT Unsigned generation counter type. Widening it to `std::uint32_t`
///         costs 2 bytes per slot and pushes the handle to 64 bits.
///
/// @warning **Generation wraparound.** After `2^(8*sizeof(GenT)) - 1` destroy
///          cycles on the *same slot*, generations repeat and a handle retained
///          across all of them can alias a new object. With the default 16-bit
///          counter that is 65535 cycles per slot. This is inherent to
///          generation tagging; use a wider `GenT` when handles are held for
///          unbounded periods.
///
/// @note Not thread-safe. Concurrent use needs external synchronisation; a
///       lock-free variant built on an atomic handle is capability-gated (it
///       needs a single-word CAS, which Cortex-M0 lacks) and is therefore
///       tracked separately — see docs/SCOPE.md.
///
/// @see object_pool for the pointer-addressed equivalent.
template <typename T, std::size_t Capacity, typename GenT = std::uint16_t>
class handle_pool {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using index_type = std::uint16_t;
  using generation_type = GenT;
  using pointer = T*;
  using const_pointer = const T*;

  /// Handle type issued by this pool. Tagged with the pool type, so a handle
  /// from a differently-parameterised pool is a compile error rather than a
  /// silent index mix-up.
  using handle_type = versioned_handle<handle_pool, index_type, GenT>;

  /// Largest `Capacity` a slot index can address, derived from the handle's own
  /// index field so the two can never drift apart.
  static constexpr size_type max_capacity = static_cast<size_type>(handle_type::max_index);

  static_assert(Capacity > 0, "metl::handle_pool requires a non-zero Capacity");
  static_assert(Capacity <= max_capacity,
                "metl::handle_pool Capacity must fit the handle's slot index field");

  /// Constructs an empty pool with every slot free and generation 1.
  handle_pool() noexcept : free_head_(0), size_(0) {
    for (size_type i = 0; i < Capacity; ++i) {
      next_[i] = static_cast<index_type>(i + 1);  // last one points at `Capacity` == end
      generation_[i] = generation_type{1};        // 0 is reserved for the null handle
      active_[i] = false;
    }
  }

  ~handle_pool() { clear(); }

  handle_pool(const handle_pool&) = delete;
  handle_pool& operator=(const handle_pool&) = delete;
  handle_pool(handle_pool&&) = delete;
  handle_pool& operator=(handle_pool&&) = delete;

  /// Constructs an object in a free slot.
  /// @return Handle to the new object, or a null handle if the pool is full
  ///         (no assert).
  template <typename... Args>
  METL_NODISCARD handle_type try_emplace(Args&&... args) {
    if (free_head_ >= Capacity) {
      return handle_type{};
    }

    const size_type index = free_head_;
    new (storage_[index].addr()) T(std::forward<Args>(args)...);
    free_head_ = next_[index];
    active_[index] = true;
    ++size_;
    return handle_type{static_cast<index_type>(index), generation_[index]};
  }

  /// Constructs an object in a free slot and returns its handle.
  /// @pre Pool is not full; a full pool asserts and aborts. Use `try_emplace`
  ///      for a non-asserting path.
  template <typename... Args>
  METL_NODISCARD handle_type emplace(Args&&... args) {
    handle_type handle = try_emplace(std::forward<Args>(args)...);
    METL_ASSERT(handle.valid());
    return handle;
  }

  /// Resolves a handle to the object it refers to.
  /// @return Pointer to the live object, or nullptr if the handle is null,
  ///         out of range, or **stale** (its slot has since been recycled).
  METL_NODISCARD pointer get(handle_type handle) noexcept {
    const size_type index = live_index(handle);
    return index < Capacity ? slot_ptr(index) : nullptr;
  }

  /// @copydoc get
  METL_NODISCARD const_pointer get(handle_type handle) const noexcept {
    const size_type index = live_index(handle);
    return index < Capacity ? slot_ptr(index) : nullptr;
  }

  /// Returns true if `handle` refers to a live object in this pool.
  METL_NODISCARD bool contains(handle_type handle) const noexcept { return live_index(handle) < Capacity; }

  /// Destroys the object a handle refers to and returns its slot to the pool.
  /// @return true if destroyed; false if the handle was null or already stale
  ///         — so a double-destroy is reported, not undefined.
  bool destroy(handle_type handle) noexcept {
    const size_type index = live_index(handle);
    if (index >= Capacity) {
      return false;
    }

    release(index);
    return true;
  }

  /// Destroys every live object and frees every slot. All outstanding handles
  /// become stale.
  void clear() noexcept {
    for (size_type i = 0; i < Capacity; ++i) {
      if (active_[i]) {
        release(i);
      }
    }
  }

  /// Returns true if no slots are in use.
  METL_NODISCARD bool empty() const noexcept { return size_ == 0; }
  /// Returns true if every slot is in use.
  METL_NODISCARD bool full() const noexcept { return size_ == Capacity; }
  /// Returns the number of live objects.
  METL_NODISCARD size_type size() const noexcept { return size_; }
  /// Returns the fixed slot count (`Capacity`).
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  /// Returns the number of free slots (`Capacity - size()`).
  METL_NODISCARD size_type available() const noexcept { return Capacity - size_; }

  /// Returns the generation a slot is currently at. Exposed for diagnostics and
  /// for tests that need to drive the counter to wraparound.
  METL_NODISCARD generation_type generation_of(index_type index) const noexcept {
    METL_ASSERT(index < Capacity);
    return generation_[index];
  }

 private:
  using storage_type = storage_for<T>;

  pointer slot_ptr(size_type index) noexcept { return storage_[index].ptr(); }
  const_pointer slot_ptr(size_type index) const noexcept { return storage_[index].ptr(); }

  /// Returns the slot index a handle resolves to, or `Capacity` when it does
  /// not resolve. One place where liveness is decided, so `get`, `contains` and
  /// `destroy` cannot drift apart.
  size_type live_index(handle_type handle) const noexcept {
    if (!handle.valid()) {
      return Capacity;
    }

    const size_type index = handle.index();
    if (index >= Capacity || !active_[index] || generation_[index] != handle.generation()) {
      return Capacity;
    }

    return index;
  }

  /// Destroys the object in `index` and pushes the slot onto the free list,
  /// bumping the generation so every outstanding handle to it goes stale.
  void release(size_type index) noexcept {
    slot_ptr(index)->~T();
    active_[index] = false;

    // Skip 0 on wraparound: generation 0 is the null-handle marker, and a slot
    // sitting at generation 0 would make every stale handle to it compare equal
    // to null rather than to the slot.
    generation_[index] = static_cast<generation_type>(generation_[index] + generation_type{1});
    if (generation_[index] == generation_type{0}) {
      generation_[index] = generation_type{1};
    }

    next_[index] = static_cast<index_type>(free_head_);
    free_head_ = index;
    --size_;
  }

  storage_type storage_[Capacity];
  index_type next_[Capacity];
  generation_type generation_[Capacity];
  bool active_[Capacity];
  size_type free_head_;
  size_type size_;
};

}  // namespace metl
