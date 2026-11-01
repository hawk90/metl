#pragma once

/// @file
/// @brief Progress guarantees for `metl::fixed_priority_queue` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `push`, `pop`, `emplace`, `try_*` | wait-free, bounded by `Capacity` and `Compare` |
///   | `top`, `size`, `empty`, `full` | wait-free, bounded |
///   | `erase_if` | wait-free, bounded by `Capacity`, the predicate and `Compare` |
///   | `clear`, destructor | wait-free, bounded by `size()` |
///   | `swap` | wait-free, bounded by the larger `size()` |
///
/// The class comment states the bound on `push` and `pop`.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/nothrow_call.hpp"
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
/// @par Reference invalidation
/// `push`/`emplace` and `pop` sift the heap and may move any element, so every
/// reference, including one from `top()`, is invalid after either. `clear`
/// invalidates all.
///
/// @tparam T Element type. Must be move-constructible and move-assignable: a heap
///           has to move elements between slots to restore its invariant.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
/// @tparam Compare Strict weak ordering. `comp(a, b) == true` means "a comes out
///                 after b".
/// @note Moving from a container, by construction or assignment, leaves the source
///       empty and usable, and destroys its elements. A pointer into the source then
///       names a destroyed object, unlike with `std::vector`.
///       The comparator is moved too, so a stateful one may leave the source unable
///       to compare (#317).
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

 private:
  // Declared ahead of the noexcept-specifiers that name it: GCC 10 and
  // older do not look ahead in the class there.
  // Whether the comparator can throw. std::less / std::greater do not declare
  // their call operator noexcept, so detail::nothrow_binary_call looks through
  // them to the operator they apply: pop() on a queue of int -- or of any type
  // whose `<` is noexcept -- keeps its noexcept (this replaced a
  // scalar-only rule).
  static constexpr bool comparator_cannot_throw = detail::nothrow_binary_call_v<Compare, const T&, const T&>;
  static constexpr bool swap_cannot_throw =
      std::is_nothrow_swappable_v<fixed_vector<T, Capacity>> && std::is_nothrow_swappable_v<Compare>;

 public:
  /// Constructs an empty queue with a default-constructed comparator.
  // noexcept follows Compare: a bare `noexcept` would report nothrow for a
  // throwing comparator and terminate when its constructor throws.
  constexpr fixed_priority_queue() noexcept(std::is_nothrow_default_constructible_v<Compare>) = default;

  /// Constructs an empty queue with an explicit comparator (for stateful ones).
  explicit fixed_priority_queue(const Compare& comp) : storage_(), comp_(comp) {}

  /// Returns true if the queue holds no elements.
  METL_NODISCARD constexpr bool empty() const noexcept { return storage_.empty(); }
  /// Returns true if the queue has reached its fixed capacity.
  METL_NODISCARD constexpr bool full() const noexcept { return storage_.full(); }
  /// Returns the number of elements currently queued.
  METL_NODISCARD constexpr size_type size() const noexcept { return storage_.size(); }
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
    keep_heap_or_clear([this] { sift_up(storage_.size() - 1); });
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
    keep_heap_or_clear([this] { sift_up(storage_.size() - 1); });
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
  /// @note `noexcept` only when nothing `pop` calls can throw: the sift moves
  ///       elements by move-construction AND move-assignment (`swap_slots`) and
  ///       calls the comparator. All three are in the condition, so a throwing
  ///       move constructor or comparator reaches the clear-and-rethrow below
  ///       instead of terminating the program.
  void pop() noexcept(std::is_nothrow_move_assignable_v<T> && std::is_nothrow_move_constructible_v<T> &&
                      comparator_cannot_throw) {
    // Never stripped: the index arithmetic below underflows on an empty queue and
    // METL_ASSERT is removed at low hardening levels.
    METL_HARDEN(!storage_.empty());
    keep_heap_or_clear([this] {
      const size_type last = storage_.size() - 1;
      if (last != 0) {
        storage_[0] = std::move(storage_[last]);
      }
      storage_.pop_back();
      if (!storage_.empty()) {
        sift_down(0);
      }
    });
  }

  /// Removes all elements.
  void clear() noexcept { storage_.clear(); }

  /// @brief Swaps contents and comparators with @p other, element by element (see
  ///        `fixed_vector::swap`).
  /// @note If an element's move or swap, or the comparators' swap, throws part-way,
  ///       neither array is a heap any more, so both queues are cleared before the
  ///       exception propagates, as for any other step that reorders the heap.
  void swap(fixed_priority_queue& other) noexcept(swap_cannot_throw) {
    if (this == &other) {
      return;
    }
#if !METL_NO_EXCEPTIONS
    if constexpr (!swap_cannot_throw) {
      try {
        swap_contents(other);
      } catch (...) {
        clear();
        other.clear();
        throw;
      }
      return;
    }
#endif
    swap_contents(other);
  }

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
    keep_heap_or_clear([&] {
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
    });
    return before - write;
  }

  /// @brief Read-only view of the underlying heap array, in heap order.
  /// @note Heap order is **not** sorted order — only `as_span()[0]` is meaningful as
  ///       "the greatest". Exposed for tests, invariant checks and serialisation,
  ///       not as an iteration order.
  METL_NODISCARD span<const T> as_span() const noexcept { return storage_.as_span(); }

 private:
  // The body of swap, without its clear-both-on-throw.
  void swap_contents(fixed_priority_queue& other) noexcept(swap_cannot_throw) {
    using std::swap;
    swap(comp_, other.comp_);
    storage_.swap(other.storage_);
  }

  /// Runs a step that reorders the heap. If an element move or the comparator
  /// throws part-way, the heap property is gone and later pops would come out in
  /// the wrong order, so the queue is cleared before rethrowing -- the same rule
  /// as flat_map. Under METL_NO_EXCEPTIONS it is a plain call.
  template <typename Step>
  void keep_heap_or_clear(Step step) {
#if METL_NO_EXCEPTIONS
    step();
#else
    try {
      step();
    } catch (...) {
      clear();
      throw;
    }
#endif
  }

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

/// Swaps the contents of two priority queues of the same type, capacity and
/// comparator. Declared only for a swappable `T` and `Compare`; otherwise
/// `using std::swap; swap(a, b)` still finds std::swap, which moves both whole
/// containers.
template <typename T,
          std::size_t N,
          typename Compare,
          std::enable_if_t<std::is_swappable_v<T> && std::is_swappable_v<Compare>, int> = 0>
inline void swap(fixed_priority_queue<T, N, Compare>& a,
                 fixed_priority_queue<T, N, Compare>& b) noexcept(noexcept(a.swap(b))) {
  a.swap(b);
}

}  // namespace metl
