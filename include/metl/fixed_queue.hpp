#pragma once

#include "metl/compiler.hpp"
#include "metl/ring_buffer.hpp"

#include <cstddef>
#include <utility>

namespace metl {

/// FIFO queue with a compile-time FIXED capacity, backed by a ring buffer.
///
/// Stores up to `Capacity` elements inline; performs NO heap allocation.
/// Pushing onto a full queue via push/emplace asserts and aborts; the `try_*`
/// variants return false instead. Popping/accessing an empty queue asserts.
/// Not thread-safe.
///
/// @tparam T Element type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
template <typename T, std::size_t Capacity>
class fixed_queue {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = const T&;

  /// Constructs an empty queue.
  constexpr fixed_queue() noexcept = default;

  /// Returns true if the queue holds no elements.
  METL_NODISCARD bool empty() const noexcept { return storage_.empty(); }
  /// Returns true if the queue has reached its fixed capacity.
  METL_NODISCARD bool full() const noexcept { return storage_.full(); }
  /// Returns the number of elements currently queued.
  METL_NODISCARD size_type size() const noexcept { return storage_.size(); }
  /// Returns the fixed capacity (`Capacity`).
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  /// Returns a reference to the front (oldest) element.
  /// @pre Queue is non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference front() noexcept { return storage_.front(); }
  /// Returns a reference to the front (oldest) element.
  /// @pre Queue is non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference front() const noexcept { return storage_.front(); }

  /// Returns a reference to the back (newest) element.
  /// @pre Queue is non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference back() noexcept { return storage_.back(); }
  /// Returns a reference to the back (newest) element.
  /// @pre Queue is non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference back() const noexcept { return storage_.back(); }

  /// Constructs an element in place at the back if there is room.
  /// @return true on success; false if the queue is full (no assert).
  template <typename... Args>
  METL_NODISCARD bool try_emplace(Args&&... args) {
    return storage_.try_emplace_back(std::forward<Args>(args)...);
  }

  /// Constructs an element in place at the back and returns a reference to it.
  /// @pre Queue is not full; overflow asserts and aborts. Use try_emplace instead.
  template <typename... Args>
  reference emplace(Args&&... args) {
    return storage_.emplace_back(std::forward<Args>(args)...);
  }

  /// Enqueues a copy of `value` if there is room; returns false when full.
  METL_NODISCARD bool try_push(const T& value) { return storage_.try_push_back(value); }
  /// Enqueues `value` by move if there is room; returns false when full.
  METL_NODISCARD bool try_push(T&& value) { return storage_.try_push_back(static_cast<T&&>(value)); }

  /// Enqueues a copy of `value`.
  /// @pre Queue is not full; overflow asserts and aborts.
  reference push(const T& value) { return storage_.emplace_back(value); }
  /// Enqueues `value` by move.
  /// @pre Queue is not full; overflow asserts and aborts.
  reference push(T&& value) { return storage_.emplace_back(static_cast<T&&>(value)); }

  /// Removes the front (oldest) element.
  /// @pre Queue is non-empty; asserts and aborts otherwise.
  void pop() noexcept { storage_.pop_front(); }

  /// Removes all elements.
  void clear() noexcept { storage_.clear(); }

 private:
  ring_buffer<T, Capacity> storage_;
};

}  // namespace metl
