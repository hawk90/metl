#pragma once

/// @file
/// @brief Progress guarantees for `metl::fixed_deque` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `push_front`, `push_back`, `pop_front`, `pop_back`, `emplace_*`, `try_*` | wait-free, bounded |
///   | element access, `size`, `empty`, `full` | wait-free, bounded |
///   | `clear`, destructor | wait-free, bounded by `size()` |
///   | `swap` | wait-free, bounded by the larger `size()` |
///
/// Both ends are index arithmetic on a ring, so a push or pop costs the same at
/// either end and does not depend on how full the deque is -- nothing is shifted.
/// `clear` runs one destructor per live element, so its bound is `size()` times
/// `~T()`, at most `Capacity` of them.
///
/// Single-threaded: this type does not synchronise.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/ring_core.hpp"
#include "metl/scope_exit.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace metl {

/// Double-ended queue with a compile-time FIXED capacity, backed by a ring.
///
/// Supports O(1) insertion and removal at both ends. Stores up to `Capacity`
/// elements inline; performs NO heap allocation. Pushing onto a full deque via
/// the non-`try_` members asserts and aborts; the `try_*` variants return false
/// instead. Accessing/popping an empty deque asserts. Not thread-safe.
///
/// Shares its ring machinery with `metl::ring_buffer` via
/// `detail::ring_core`; that base supplies front/back/at/operator[],
/// try_emplace_back/emplace_back/try_push_back, pop_front, clear, capacity,
/// and the value-semantic copy/move operations. `fixed_deque` adds front
/// insertion and back removal.
///
/// @par Iterator invalidation
/// Elements never move: adding or removing at either end builds or destroys
/// only that element, so pointers and references to the others stay valid.
/// An iterator, though, holds a position counted from the front, so
/// `pop_front` and `emplace_front`/`push_front` shift what every iterator
/// refers to by one place. Operations at the back keep iterators to existing
/// elements valid and invalidate `end()`; `clear` invalidates all.
///
/// @tparam T Element type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
/// @note Moving from a container, by construction or assignment, leaves the source
///       empty and usable, and destroys its elements. A pointer into the source then
///       names a destroyed object, unlike with `std::vector`.
template <typename T, std::size_t Capacity>
class fixed_deque : public detail::ring_core<T, Capacity> {
  using base = detail::ring_core<T, Capacity>;

 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = const T&;

  /// Constructs an empty deque.
  constexpr fixed_deque() noexcept = default;

  /// Returns true if the deque holds no elements.
  METL_NODISCARD constexpr bool empty() const noexcept { return this->size_ == 0; }
  /// Returns true if the deque has reached its fixed capacity.
  METL_NODISCARD constexpr bool full() const noexcept { return this->size_ == Capacity; }
  /// Returns the number of elements currently stored.
  METL_NODISCARD constexpr size_type size() const noexcept { return this->size_; }

  /// Constructs an element in place at the front if there is room.
  /// @return true on success; false if the deque is full (no assert).
  template <typename... Args>
  METL_NODISCARD bool try_emplace_front(Args&&... args) {
    if (full()) {
      return false;
    }
    // Claim the slot before T's constructor runs, so a constructor that pushes
    // into this deque gets another one. If the constructor throws the claim is
    // undone, so head_ never stays on an unconstructed slot.
    const auto previous_head = this->head_;
    const auto slot = this->retreat(this->head_);
    this->head_ = slot;
    ++this->size_;
    auto undo = make_scope_exit([this, previous_head]() noexcept {
      this->head_ = previous_head;
      --this->size_;
    });
    ::new (this->storage_[slot].addr()) T(std::forward<Args>(args)...);
    undo.release();
    return true;
  }

  /// Constructs an element in place at the front and returns a reference to it.
  /// @pre Deque is not full; overflow asserts and aborts. Use try_emplace_front instead.
  template <typename... Args>
  reference emplace_front(Args&&... args) {
    // Where the element will be built, taken before T's constructor runs: if
    // that constructor pushes to the front, front() is the inner element.
    const auto slot = this->retreat(this->head_);
    const bool inserted = try_emplace_front(std::forward<Args>(args)...);
    METL_ASSERT(inserted);
    if (!inserted) {
      // With METL_ASSERT stripped, a full deque returns its front element -- an
      // object, unless Capacity is 0 and there is none to return.
      METL_HARDEN(this->size_ > 0);
      return this->front();
    }
    return this->storage_at(slot);
  }

  /// Prepends a copy of `value` at the front if there is room; false when full.
  METL_NODISCARD bool try_push_front(const T& value) { return try_emplace_front(value); }
  /// Prepends `value` by move at the front if there is room; false when full.
  METL_NODISCARD bool try_push_front(T&& value) { return try_emplace_front(static_cast<T&&>(value)); }

  /// Prepends a copy of `value` at the front. @pre Not full; overflow asserts.
  reference push_front(const T& value) { return emplace_front(value); }
  /// Prepends `value` by move at the front. @pre Not full; overflow asserts.
  reference push_front(T&& value) { return emplace_front(static_cast<T&&>(value)); }

  /// Removes the back element.
  /// @note The element leaves the container before its destructor runs, so a
  ///       destructor that calls back to remove elements sees it gone. It must
  ///       not insert: the slot being destroyed may be the next one handed out.
  /// @pre Deque is non-empty; asserts and aborts otherwise.
  void pop_back() noexcept {
    // Hard, not METL_ASSERT: an empty pop at METL_HARDENING_NONE destroyed a
    // dead slot and wrapped size_ to SIZE_MAX -- the next push leaked, and a
    // destructor then looped ~2^64 times (fixed_vector's
    // pop_back has the same guard).
    METL_HARDEN(this->size_ > 0);
    --this->size_;  // shrink first, then destroy, as in pop_front
    this->storage_at(this->physical_index(this->size_)).~T();
  }

  /// Swaps contents with `other` element by element: the elements both hold
  /// are swapped in place, and the longer one's remaining elements are moved
  /// across. Each keeps its own storage, so an iterator keeps its position,
  /// and positions past the new size are invalid.
  /// @note If an element's move or swap throws, both stay valid, holding
  ///       elements of either, and nothing leaks.
  void swap(fixed_deque& other) noexcept(base::swap_cannot_throw) { this->swap_core(other); }
};

/// Swaps the contents of two deques of the same type and capacity.
/// Declared only for a swappable `T`; otherwise `using std::swap; swap(a, b)`
/// still finds std::swap, which moves both whole containers.
template <typename T, std::size_t N, std::enable_if_t<std::is_swappable_v<T>, int> = 0>
inline void swap(fixed_deque<T, N>& a, fixed_deque<T, N>& b) noexcept(noexcept(a.swap(b))) {
  a.swap(b);
}

}  // namespace metl
