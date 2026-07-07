#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <new>
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
/// @tparam T Element type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
template <typename T, std::size_t Capacity>
class ring_buffer {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = const T&;

  /// Constructs an empty ring buffer.
  constexpr ring_buffer() noexcept : head_(0), size_(0) {}

  ~ring_buffer() { clear(); }

  /// Copy-constructs by copying each element of `other` in order.
  ring_buffer(const ring_buffer& other) : head_(0), size_(0) {
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(other.at(i));
    }
  }

  /// Move-constructs by moving each element out of `other`, leaving it empty.
  ring_buffer(ring_buffer&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
      : head_(0), size_(0) {
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(static_cast<T&&>(other.at(i)));
    }
    other.clear();
  }

  ring_buffer& operator=(const ring_buffer& other) {
    if (this == &other) {
      return *this;
    }

    clear();
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(other.at(i));
    }
    return *this;
  }

  ring_buffer& operator=(ring_buffer&& other) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                                       std::is_nothrow_move_assignable<T>::value) {
    if (this == &other) {
      return *this;
    }

    clear();
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(static_cast<T&&>(other.at(i)));
    }
    other.clear();
    return *this;
  }

  /// Returns true if the buffer holds no elements.
  METL_NODISCARD constexpr bool empty() const noexcept { return size_ == 0; }
  /// Returns true if the buffer has reached its fixed capacity.
  METL_NODISCARD constexpr bool full() const noexcept { return size_ == Capacity; }
  /// Returns the number of elements currently stored.
  METL_NODISCARD constexpr size_type size() const noexcept { return size_; }
  /// Returns the fixed capacity (`Capacity`).
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  /// Returns a reference to the front (oldest) element.
  /// @pre Buffer is non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference front() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(head_);
  }

  /// Returns a reference to the front (oldest) element.
  /// @pre Buffer is non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference front() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(head_);
  }

  /// Returns a reference to the back (newest) element.
  /// @pre Buffer is non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference back() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(physical_index(size_ - 1));
  }

  /// Returns a reference to the back (newest) element.
  /// @pre Buffer is non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference back() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(physical_index(size_ - 1));
  }

  /// Accesses the element at logical `index` (0 == front).
  /// @pre `index < size()`; out-of-range asserts and aborts (does not throw).
  METL_NODISCARD reference at(size_type index) noexcept {
    METL_ASSERT(index < size_);
    return storage_at(physical_index(index));
  }

  /// Accesses the element at logical `index` (0 == front).
  /// @pre `index < size()`; out-of-range asserts and aborts (does not throw).
  METL_NODISCARD const_reference at(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return storage_at(physical_index(index));
  }

  /// Accesses the element at logical `index`. @pre `index < size()`; asserts otherwise.
  METL_NODISCARD reference operator[](size_type index) noexcept { return at(index); }
  /// Accesses the element at logical `index`. @pre `index < size()`; asserts otherwise.
  METL_NODISCARD const_reference operator[](size_type index) const noexcept { return at(index); }

  /// Constructs an element in place at the back if there is room.
  /// @return true on success; false if the buffer is full (no assert).
  template <typename... Args>
  METL_NODISCARD bool try_emplace_back(Args&&... args) {
    if (full()) {
      return false;
    }

    new (storage_[physical_index(size_)].addr()) T(std::forward<Args>(args)...);
    ++size_;
    return true;
  }

  /// Constructs an element in place at the back and returns a reference to it.
  /// @pre Buffer is not full; overflow asserts and aborts. Use try_emplace_back
  /// or push_overwrite instead.
  template <typename... Args>
  reference emplace_back(Args&&... args) {
    const bool inserted = try_emplace_back(std::forward<Args>(args)...);
    METL_ASSERT(inserted);
    (void)inserted;
    return back();
  }

  /// Appends a copy of `value` at the back if there is room; false when full.
  METL_NODISCARD bool try_push_back(const T& value) { return try_emplace_back(value); }
  /// Appends `value` by move at the back if there is room; false when full.
  METL_NODISCARD bool try_push_back(T&& value) { return try_emplace_back(static_cast<T&&>(value)); }

  /// Constructs an element at the back, evicting the oldest element if full.
  /// @return Reference to the newly constructed element. Never asserts on a full
  /// buffer (contrast emplace_back).
  template <typename... Args>
  reference push_overwrite(Args&&... args) {
    if (full()) {
      pop_front();
    }

    return emplace_back(std::forward<Args>(args)...);
  }

  /// Removes the front (oldest) element.
  /// @pre Buffer is non-empty; asserts and aborts otherwise.
  void pop_front() noexcept {
    METL_ASSERT(size_ > 0);
    storage_at(head_).~T();
    head_ = advance(head_);
    --size_;
  }

  /// Removes all elements.
  void clear() noexcept {
    while (!empty()) {
      pop_front();
    }
  }

 private:
  using storage_type = storage_for<T>;

  constexpr size_type advance(size_type index) const noexcept {
    return Capacity == 0 ? 0 : (index + 1) % Capacity;
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
