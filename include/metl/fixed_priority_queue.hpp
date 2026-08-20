#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/fixed_vector.hpp"
#include "metl/span.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace metl {

/// @brief Fixed-capacity binary heap: the largest element per @c Compare is always
///        on top. No heap allocation; storage is inline.
///
/// The shape `std::priority_queue` has, with METL's contracts. Backed by a
/// `fixed_vector`, so the same inline storage, ASan tail poisoning and
/// construct/destroy discipline apply.
///
/// **Progress guarantee (SCOPE.md I3): wait-free, bounded.** `push`/`emplace`/`pop`
/// each touch at most `floor(log2(Capacity))` levels, and `Capacity` is a
/// compile-time constant, so the worst case is known at compile time — there is no
/// rehash cliff and no reallocation. The one thing METL cannot bound for you is
/// @c Compare itself: it is invoked O(log Capacity) times per operation, so a
/// comparator with an unbounded body makes the whole operation unbounded.
///
/// **Ordering is a max-heap by default**, like `std::priority_queue`. For a
/// min-heap — the usual case for deadlines and timers — pass `std::greater<T>`:
/// @code
/// metl::fixed_priority_queue<deadline, 16, std::greater<deadline>> timers;
/// @endcode
///
/// **Not stable.** Elements that compare equivalent come out in an unspecified
/// order, and that order may differ between two queues built from the same
/// insertions. If you need a tiebreak, put it in the element and in @c Compare.
///
/// @tparam T Element type. Must be move-constructible and move-assignable: a heap
///           has to move elements between slots to restore its invariant.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
/// @tparam Compare Strict weak ordering. `comp(a, b) == true` means "a comes out
///                 after b".
template <typename T, std::size_t Capacity, typename Compare = std::less<T>>
class fixed_priority_queue {
  static_assert(std::is_move_constructible_v<T>,
                "fixed_priority_queue<T> requires a move-constructible T: restoring the heap "
                "invariant moves elements between slots");
  static_assert(std::is_move_assignable_v<T>,
                "fixed_priority_queue<T> requires a move-assignable T: restoring the heap "
                "invariant moves elements between slots");
  // Child index is 2*i+1 with i <= Capacity-1, so the largest index computed is
  // 2*Capacity-1. Refuse a capacity where that would wrap rather than silently
  // indexing garbage.
  static_assert(Capacity <= (static_cast<std::size_t>(-1) - 1) / 2,
                "fixed_priority_queue Capacity is too large: the child index 2*i+1 would "
                "overflow size_type");

 public:
  using value_type = T;
  using size_type = std::size_t;
  using const_reference = const T&;
  using value_compare = Compare;

  /// Constructs an empty queue with a default-constructed comparator.
  fixed_priority_queue() = default;

  /// Constructs an empty queue with an explicit comparator (for stateful ones).
  explicit fixed_priority_queue(const Compare& comp) : storage_(), comp_(comp) {}

  /// Returns true if the queue holds no elements.
  METL_NODISCARD bool empty() const noexcept { return storage_.empty(); }
  /// Returns true if the queue has reached its fixed capacity.
  METL_NODISCARD bool full() const noexcept { return storage_.full(); }
  /// Returns the number of elements currently queued.
  METL_NODISCARD size_type size() const noexcept { return storage_.size(); }
  /// Returns the fixed capacity (`Capacity`).
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  /// @brief The greatest element per @c Compare.
  /// @pre Queue is non-empty; asserts and aborts otherwise. Check `empty()` first.
  /// @note Const-only, deliberately, and unlike `fixed_stack::top()`. Handing out a
  ///       mutable reference would let a caller change the key the heap is ordered
  ///       by, silently breaking the invariant for every later operation with no
  ///       diagnostic. `std::priority_queue::top()` is const for the same reason.
  ///       To change the top element's priority: `pop()`, adjust, `push()`.
  METL_NODISCARD const_reference top() const noexcept { return storage_.front(); }

  /// @brief Constructs an element in place if there is room.
  /// @return true on success; false if the queue is full (contents unchanged).
  template <typename... Args>
  METL_NODISCARD bool try_emplace(Args&&... args) {
    if (!storage_.try_emplace_back(std::forward<Args>(args)...)) {
      return false;
    }
    sift_up(storage_.size() - 1);
    return true;
  }

  /// @brief Constructs an element in place.
  /// @pre Queue is not full; overflow asserts and aborts. Use try_emplace otherwise.
  /// @note Returns void, unlike `fixed_stack::emplace`. The new element is sifted
  ///       into position immediately, so its slot is not the one it was constructed
  ///       in — a returned reference would point at whatever the sift moved there.
  template <typename... Args>
  void emplace(Args&&... args) {
    storage_.emplace_back(std::forward<Args>(args)...);
    sift_up(storage_.size() - 1);
  }

  /// Pushes a copy of `value` if there is room; false when full (contents unchanged).
  METL_NODISCARD bool try_push(const T& value) { return try_emplace(value); }
  /// Pushes `value` by move if there is room; false when full (`value` is not moved from).
  METL_NODISCARD bool try_push(T&& value) { return try_emplace(std::move(value)); }

  /// Pushes a copy of `value`.
  /// @pre Queue is not full; overflow asserts and aborts. See the note on `emplace`
  ///      for why this returns void.
  void push(const T& value) { emplace(value); }
  /// Pushes `value` by move.
  /// @pre Queue is not full; overflow asserts and aborts.
  void push(T&& value) { emplace(std::move(value)); }

  /// @brief Removes the greatest element.
  /// @pre Queue is non-empty; asserts and aborts otherwise.
  /// @note No `try_pop`: on a single-threaded container `if (!q.empty()) q.pop();`
  ///       is already an exact, non-racy pre-check (SCOPE.md section 9, R5).
  void pop() noexcept(std::is_nothrow_move_assignable_v<T>) {
    // Never stripped: the index arithmetic below underflows on an empty queue and
    // METL_ASSERT is removed at low hardening levels.
    METL_HARDEN(!storage_.empty());
    const size_type last = storage_.size() - 1;
    if (last != 0) {
      storage_[0] = std::move(storage_[last]);
    }
    storage_.pop_back();
    if (!storage_.empty()) {
      sift_down(0);
    }
  }

  /// Removes all elements.
  void clear() noexcept { storage_.clear(); }

  /// @brief Removes every element satisfying @p pred, then restores the heap.
  /// @return How many elements were removed.
  /// @note This is the only way to remove something that is not on top, and it is
  ///       deliberately O(Capacity) rather than O(log Capacity): a heap has no
  ///       ordering to search by, so finding the victims is a full scan either way.
  ///       Still bounded at compile time, so I3 holds. Compacting first and
  ///       re-heapifying by Floyd's method keeps it O(n) moves rather than
  ///       n sift-downs.
  /// @warning @p pred must not modify the queue.
  template <typename Pred>
  size_type erase_if(Pred pred) {
    const size_type before = storage_.size();
    size_type write = 0;
    for (size_type read = 0; read < before; ++read) {
      if (!pred(static_cast<const T&>(storage_[read]))) {
        if (write != read) {
          storage_[write] = std::move(storage_[read]);
        }
        ++write;
      }
    }
    while (storage_.size() > write) {
      storage_.pop_back();
    }
    heapify();
    return before - write;
  }

  /// @brief Read-only view of the underlying heap array, in heap order.
  /// @note Heap order is **not** sorted order — only `data()[0]` is meaningful as
  ///       "the greatest". Exposed for tests, invariant checks and serialisation,
  ///       not as an iteration order.
  METL_NODISCARD span<const T> as_span() const noexcept { return storage_.as_span(); }

 private:
  /// Move the element at `index` toward the root until its parent outranks it.
  void sift_up(size_type index) {
    while (index > 0) {
      const size_type parent = (index - 1) / 2;
      if (!comp_(storage_[parent], storage_[index])) {
        return;  // parent already outranks the child: done
      }
      swap_slots(parent, index);
      index = parent;
    }
  }

  /// Move the element at `index` toward the leaves until both children are outranked.
  void sift_down(size_type index) {
    const size_type count = storage_.size();
    for (;;) {
      const size_type left = (2 * index) + 1;
      if (left >= count) {
        return;  // no children
      }
      const size_type right = left + 1;
      size_type best = left;
      if (right < count && comp_(storage_[left], storage_[right])) {
        best = right;
      }
      if (!comp_(storage_[index], storage_[best])) {
        return;  // already outranks the better child: done
      }
      swap_slots(index, best);
      index = best;
    }
  }

  /// Floyd's build-heap: sift down every internal node, deepest first. O(n), where
  /// n sift-downs from the top would be O(n log n).
  void heapify() {
    size_type index = storage_.size() / 2;
    while (index > 0) {
      --index;
      sift_down(index);
    }
  }

  /// Three moves rather than a hole-shifting sift. Capacity is bounded and small,
  /// so the depth is a handful of levels; the extra moves cost less than the
  /// subtlety of a hole-based implementation would.
  void swap_slots(size_type a, size_type b) {
    T temporary = std::move(storage_[a]);
    storage_[a] = std::move(storage_[b]);
    storage_[b] = std::move(temporary);
  }

  fixed_vector<T, Capacity> storage_;
  Compare comp_;
};

}  // namespace metl
