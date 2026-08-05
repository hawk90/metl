#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/ring_core.hpp"

#include <cstddef>
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
/// @tparam T Element type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
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
  METL_NODISCARD bool empty() const noexcept { return this->size_ == 0; }
  /// Returns true if the deque has reached its fixed capacity.
  METL_NODISCARD bool full() const noexcept { return this->size_ == Capacity; }
  /// Returns the number of elements currently stored.
  METL_NODISCARD size_type size() const noexcept { return this->size_; }

  /// Constructs an element in place at the front if there is room.
  /// @return true on success; false if the deque is full (no assert).
  template <typename... Args>
  METL_NODISCARD bool try_emplace_front(Args&&... args) {
    if (full()) {
      return false;
    }

    this->head_ = this->retreat(this->head_);
    new (this->storage_[this->head_].addr()) T(std::forward<Args>(args)...);
    ++this->size_;
    return true;
  }

  /// Constructs an element in place at the front and returns a reference to it.
  /// @pre Deque is not full; overflow asserts and aborts. Use try_emplace_front instead.
  template <typename... Args>
  reference emplace_front(Args&&... args) {
    const bool inserted = try_emplace_front(std::forward<Args>(args)...);
    METL_ASSERT(inserted);
    (void)inserted;
    return this->front();
  }

  /// Prepends a copy of `value` at the front if there is room; false when full.
  METL_NODISCARD bool try_push_front(const T& value) { return try_emplace_front(value); }
  /// Prepends `value` by move at the front if there is room; false when full.
  METL_NODISCARD bool try_push_front(T&& value) { return try_emplace_front(static_cast<T&&>(value)); }

  /// Appends a copy of `value` at the back. @pre Not full; overflow asserts.
  reference push_back(const T& value) { return this->emplace_back(value); }
  /// Appends `value` by move at the back. @pre Not full; overflow asserts.
  reference push_back(T&& value) { return this->emplace_back(static_cast<T&&>(value)); }
  /// Prepends a copy of `value` at the front. @pre Not full; overflow asserts.
  reference push_front(const T& value) { return emplace_front(value); }
  /// Prepends `value` by move at the front. @pre Not full; overflow asserts.
  reference push_front(T&& value) { return emplace_front(static_cast<T&&>(value)); }

  /// Removes the back element.
  /// @pre Deque is non-empty; asserts and aborts otherwise.
  void pop_back() noexcept {
    METL_ASSERT(this->size_ > 0);
    this->storage_at(this->physical_index(this->size_ - 1)).~T();
    --this->size_;
  }
};

}  // namespace metl
