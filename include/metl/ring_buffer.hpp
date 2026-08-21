#pragma once

/// @file
/// @brief Progress guarantees for `metl::ring_buffer` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `push_overwrite`, `push_back`, `pop_front`, `emplace_*`, `try_*` | wait-free, bounded |
///   | element access, `size`, `empty`, `full` | wait-free, bounded |
///   | `clear`, destructor | wait-free, bounded by `size()` |
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
  template <typename... Args>
  reference push_overwrite(Args&&... args) {
    if (full()) {
      this->pop_front();
    }

    return this->emplace_back(std::forward<Args>(args)...);
  }
};

}  // namespace metl
