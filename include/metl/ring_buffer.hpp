#pragma once

/// @file
/// @brief Progress guarantees for `metl::ring_buffer` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `push_overwrite`, `emplace_back`, `pop_front`, `try_*` | wait-free, bounded |
///   | element access, `size`, `empty`, `full` | wait-free, bounded |
///   | `clear`, destructor | wait-free, bounded by `size()` |
///   | `swap` | wait-free, bounded by the larger `size()` |
///
/// Every position is index arithmetic on a ring, so nothing is shifted and the cost
/// does not depend on how full the buffer is. `push_overwrite` on a full buffer
/// destroys the oldest element and constructs the new one in its place -- still a
/// fixed number of steps, plus `~T()`.
///
/// Single-threaded: this type does not synchronise. For a byte stream across an ISR
/// boundary, use `metl::spsc_byte_ring`.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/ring_core.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace metl {

/// Circular FIFO buffer with a compile-time FIXED capacity.
///
/// Stores up to `Capacity` elements inline; performs NO heap allocation.
/// try_emplace_back / try_push_back reject a full buffer by returning false,
/// while emplace_back asserts and aborts on overflow. push_overwrite instead
/// evicts the oldest element to make room. Not thread-safe.
///
/// Shares its ring machinery with `metl::fixed_deque` via
/// `detail::ring_core`; that base supplies front/back/at/operator[],
/// try_emplace_back/emplace_back/try_push_back, pop_front, clear, capacity,
/// and the value-semantic copy/move operations.
///
/// @par Iterator invalidation
/// Elements never move: adding at the back or removing at the front builds or
/// destroys only that element, so pointers and references to the others stay
/// valid. An iterator, though, holds a position counted from the front, so
/// `pop_front`, and a `push_overwrite` that drops the oldest element, shift
/// what every iterator refers to by one place. `emplace_back`/`push_back` keep
/// iterators to existing elements valid and invalidate `end()`; `clear`
/// invalidates all.
///
/// @tparam T Element type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
template <typename T, std::size_t Capacity>
class ring_buffer : public detail::ring_core<T, Capacity> {
  using base = detail::ring_core<T, Capacity>;

 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = const T&;

  /// Constructs an empty ring buffer.
  constexpr ring_buffer() noexcept = default;

  /// Returns true if the buffer holds no elements.
  METL_NODISCARD constexpr bool empty() const noexcept { return this->size_ == 0; }
  /// Returns true if the buffer has reached its fixed capacity.
  METL_NODISCARD constexpr bool full() const noexcept { return this->size_ == Capacity; }
  /// Returns the number of elements currently stored.
  METL_NODISCARD constexpr size_type size() const noexcept { return this->size_; }

  /// Constructs an element at the back, evicting the oldest element if full.
  /// @return Reference to the newly constructed element. Never asserts on a full
  /// buffer (contrast emplace_back).
  /// @pre If `T` is not move-constructible and the buffer is full, `args` must not
  ///      refer to `front()` or into it: the oldest element is destroyed before the
  ///      new one is constructed. A move-constructible `T` is built first, so it
  ///      has no such restriction.
  /// A zero-capacity buffer has no element to evict or return, so calling it
  /// there is a compile error.
  template <typename... Args>
  reference push_overwrite(Args&&... args) {
    static_assert(Capacity > 0,
                  "metl::ring_buffer::push_overwrite needs Capacity > 0: a zero-capacity buffer has no slot "
                  "to overwrite and no element to return");
    if (full()) {
      if constexpr (std::is_move_constructible_v<T>) {
        // Build the value before evicting: `args` may refer to the element
        // being evicted (`rb.push_overwrite(rb.front())`).
        T value(std::forward<Args>(args)...);
        this->pop_front();
        return this->emplace_back(static_cast<T&&>(value));
      } else {
        // A non-movable T cannot be built aside; constructing it from the
        // element it replaces is the caller's to avoid.
        this->pop_front();
      }
    }

    return this->emplace_back(std::forward<Args>(args)...);
  }

  /// Swaps contents with `other` element by element: the elements both hold
  /// are swapped in place, and the longer one's remaining elements are moved
  /// across. Each keeps its own storage, so an iterator keeps its position,
  /// and positions past the new size are invalid.
  /// @note If an element's move or swap throws, both stay valid, holding
  ///       elements of either, and nothing leaks.
  void swap(ring_buffer& other) noexcept(base::swap_cannot_throw) { this->swap_core(other); }
};

/// Swaps the contents of two ring buffers of the same type and capacity.
/// Declared only for a swappable `T`; otherwise `using std::swap; swap(a, b)`
/// still finds std::swap, which moves both whole containers.
template <typename T, std::size_t N, std::enable_if_t<std::is_swappable_v<T>, int> = 0>
inline void swap(ring_buffer<T, N>& a, ring_buffer<T, N>& b) noexcept(noexcept(a.swap(b))) {
  a.swap(b);
}

}  // namespace metl
