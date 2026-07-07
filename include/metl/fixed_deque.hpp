#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <new>
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
/// @tparam T Element type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
template <typename T, std::size_t Capacity>
class fixed_deque {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = const T&;

  /// Constructs an empty deque.
  constexpr fixed_deque() noexcept : head_(0), size_(0) {}

  ~fixed_deque() { clear(); }

  /// Copy-constructs by copying each element of `other` in order.
  fixed_deque(const fixed_deque& other) : head_(0), size_(0) {
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(other[i]);
    }
  }

  /// Move-constructs by moving each element out of `other`, leaving it empty.
  fixed_deque(fixed_deque&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
      : head_(0), size_(0) {
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(static_cast<T&&>(other[i]));
    }
    other.clear();
  }

  fixed_deque& operator=(const fixed_deque& other) {
    if (this == &other) {
      return *this;
    }

    clear();
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(other[i]);
    }
    return *this;
  }

  fixed_deque& operator=(fixed_deque&& other) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                                       std::is_nothrow_move_assignable<T>::value) {
    if (this == &other) {
      return *this;
    }

    clear();
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(static_cast<T&&>(other[i]));
    }
    other.clear();
    return *this;
  }

  /// Returns true if the deque holds no elements.
  METL_NODISCARD bool empty() const noexcept { return size_ == 0; }
  /// Returns true if the deque has reached its fixed capacity.
  METL_NODISCARD bool full() const noexcept { return size_ == Capacity; }
  /// Returns the number of elements currently stored.
  METL_NODISCARD size_type size() const noexcept { return size_; }
  /// Returns the fixed capacity (`Capacity`).
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  /// Returns a reference to the front element.
  /// @pre Deque is non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference front() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(head_);
  }

  /// Returns a reference to the front element.
  /// @pre Deque is non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference front() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(head_);
  }

  /// Returns a reference to the back element.
  /// @pre Deque is non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference back() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(physical_index(size_ - 1));
  }

  /// Returns a reference to the back element.
  /// @pre Deque is non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference back() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(physical_index(size_ - 1));
  }

  /// Accesses the element at logical `index` (0 == front).
  /// @pre `index < size()`; out-of-range asserts and aborts (does not throw).
  METL_NODISCARD reference operator[](size_type index) noexcept {
    METL_ASSERT(index < size_);
    return storage_at(physical_index(index));
  }

  /// Accesses the element at logical `index` (0 == front).
  /// @pre `index < size()`; out-of-range asserts and aborts (does not throw).
  METL_NODISCARD const_reference operator[](size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return storage_at(physical_index(index));
  }

  /// Accesses the element at logical `index`. Unlike std::deque::at, does NOT
  /// throw: an out-of-range index asserts and aborts by default.
  /// @pre `index < size()`.
  METL_NODISCARD reference at(size_type index) noexcept {
    METL_ASSERT(index < size_);
    return storage_at(physical_index(index));
  }

  /// Accesses the element at logical `index`. Unlike std::deque::at, does NOT
  /// throw: an out-of-range index asserts and aborts by default.
  /// @pre `index < size()`.
  METL_NODISCARD const_reference at(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return storage_at(physical_index(index));
  }

  /// Constructs an element in place at the back if there is room.
  /// @return true on success; false if the deque is full (no assert).
  template <typename... Args>
  METL_NODISCARD bool try_emplace_back(Args&&... args) {
    if (full()) {
      return false;
    }

    new (storage_[physical_index(size_)].addr()) T(std::forward<Args>(args)...);
    ++size_;
    return true;
  }

  /// Constructs an element in place at the front if there is room.
  /// @return true on success; false if the deque is full (no assert).
  template <typename... Args>
  METL_NODISCARD bool try_emplace_front(Args&&... args) {
    if (full()) {
      return false;
    }

    head_ = retreat(head_);
    new (storage_[head_].addr()) T(std::forward<Args>(args)...);
    ++size_;
    return true;
  }

  /// Constructs an element in place at the back and returns a reference to it.
  /// @pre Deque is not full; overflow asserts and aborts. Use try_emplace_back instead.
  template <typename... Args>
  reference emplace_back(Args&&... args) {
    const bool inserted = try_emplace_back(std::forward<Args>(args)...);
    METL_ASSERT(inserted);
    (void)inserted;
    return back();
  }

  /// Constructs an element in place at the front and returns a reference to it.
  /// @pre Deque is not full; overflow asserts and aborts. Use try_emplace_front instead.
  template <typename... Args>
  reference emplace_front(Args&&... args) {
    const bool inserted = try_emplace_front(std::forward<Args>(args)...);
    METL_ASSERT(inserted);
    (void)inserted;
    return front();
  }

  /// Appends a copy of `value` at the back if there is room; false when full.
  METL_NODISCARD bool try_push_back(const T& value) { return try_emplace_back(value); }
  /// Appends `value` by move at the back if there is room; false when full.
  METL_NODISCARD bool try_push_back(T&& value) { return try_emplace_back(static_cast<T&&>(value)); }
  /// Prepends a copy of `value` at the front if there is room; false when full.
  METL_NODISCARD bool try_push_front(const T& value) { return try_emplace_front(value); }
  /// Prepends `value` by move at the front if there is room; false when full.
  METL_NODISCARD bool try_push_front(T&& value) { return try_emplace_front(static_cast<T&&>(value)); }

  /// Appends a copy of `value` at the back. @pre Not full; overflow asserts.
  reference push_back(const T& value) { return emplace_back(value); }
  /// Appends `value` by move at the back. @pre Not full; overflow asserts.
  reference push_back(T&& value) { return emplace_back(static_cast<T&&>(value)); }
  /// Prepends a copy of `value` at the front. @pre Not full; overflow asserts.
  reference push_front(const T& value) { return emplace_front(value); }
  /// Prepends `value` by move at the front. @pre Not full; overflow asserts.
  reference push_front(T&& value) { return emplace_front(static_cast<T&&>(value)); }

  /// Removes the front element.
  /// @pre Deque is non-empty; asserts and aborts otherwise.
  void pop_front() noexcept {
    METL_ASSERT(size_ > 0);
    storage_at(head_).~T();
    head_ = advance(head_);
    --size_;
  }

  /// Removes the back element.
  /// @pre Deque is non-empty; asserts and aborts otherwise.
  void pop_back() noexcept {
    METL_ASSERT(size_ > 0);
    storage_at(physical_index(size_ - 1)).~T();
    --size_;
  }

  /// Removes all elements.
  void clear() noexcept {
    while (!empty()) {
      pop_back();
    }
    head_ = 0;
  }

 private:
  using storage_type = storage_for<T>;

  constexpr size_type advance(size_type index) const noexcept {
    return Capacity == 0 ? 0 : (index + 1) % Capacity;
  }

  constexpr size_type retreat(size_type index) const noexcept {
    return Capacity == 0 ? 0 : (index == 0 ? Capacity - 1 : index - 1);
  }

  constexpr size_type physical_index(size_type logical_index) const noexcept {
    return Capacity == 0 ? 0 : (head_ + logical_index) % Capacity;
  }

  T& storage_at(size_type index) noexcept { return storage_[index].ref(); }
  const T& storage_at(size_type index) const noexcept { return storage_[index].ref(); }

  storage_type storage_[Capacity == 0 ? 1 : Capacity];
  size_type head_;
  size_type size_;
};

}  // namespace metl
