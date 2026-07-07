#pragma once

#include "metl/compiler.hpp"
#include "metl/fixed_vector.hpp"

#include <cstddef>
#include <utility>

namespace metl {

/// LIFO stack with a compile-time FIXED capacity, backed by a fixed_vector.
///
/// Stores up to `Capacity` elements inline; performs NO heap allocation.
/// Pushing onto a full stack via push/emplace asserts and aborts; the `try_*`
/// variants return false instead. Accessing/popping an empty stack asserts.
/// Not thread-safe.
///
/// @tparam T Element type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
template <typename T, std::size_t Capacity>
class fixed_stack {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = const T&;

  /// Constructs an empty stack.
  constexpr fixed_stack() noexcept = default;

  /// Returns true if the stack holds no elements.
  METL_NODISCARD bool empty() const noexcept { return storage_.empty(); }
  /// Returns true if the stack has reached its fixed capacity.
  METL_NODISCARD bool full() const noexcept { return storage_.full(); }
  /// Returns the number of elements currently on the stack.
  METL_NODISCARD size_type size() const noexcept { return storage_.size(); }
  /// Returns the fixed capacity (`Capacity`).
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  /// Returns a reference to the top (most recently pushed) element.
  /// @pre Stack is non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference top() noexcept { return storage_.back(); }
  /// Returns a reference to the top (most recently pushed) element.
  /// @pre Stack is non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference top() const noexcept { return storage_.back(); }

  /// Constructs an element in place on top if there is room.
  /// @return true on success; false if the stack is full (no assert).
  template <typename... Args>
  METL_NODISCARD bool try_emplace(Args&&... args) {
    return storage_.try_emplace_back(std::forward<Args>(args)...);
  }

  /// Constructs an element in place on top and returns a reference to it.
  /// @pre Stack is not full; overflow asserts and aborts. Use try_emplace instead.
  template <typename... Args>
  reference emplace(Args&&... args) {
    return storage_.emplace_back(std::forward<Args>(args)...);
  }

  /// Pushes a copy of `value` if there is room; returns false when full.
  METL_NODISCARD bool try_push(const T& value) { return storage_.try_push_back(value); }
  /// Pushes `value` by move if there is room; returns false when full.
  METL_NODISCARD bool try_push(T&& value) { return storage_.try_push_back(static_cast<T&&>(value)); }

  /// Pushes a copy of `value`.
  /// @pre Stack is not full; overflow asserts and aborts.
  reference push(const T& value) { return storage_.push_back(value); }
  /// Pushes `value` by move.
  /// @pre Stack is not full; overflow asserts and aborts.
  reference push(T&& value) { return storage_.push_back(static_cast<T&&>(value)); }

  /// Removes the top element.
  /// @pre Stack is non-empty; asserts and aborts otherwise.
  void pop() noexcept { storage_.pop_back(); }

  /// Removes all elements.
  void clear() noexcept { storage_.clear(); }

 private:
  fixed_vector<T, Capacity> storage_;
};

}  // namespace metl
