#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

template <typename T, std::size_t Capacity>
class static_message_queue {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = const T&;

  constexpr static_message_queue() noexcept : head_(0), tail_(0), size_(0) {}

  ~static_message_queue() { clear(); }

  static_message_queue(const static_message_queue& other) : head_(0), tail_(0), size_(0) {
    for (size_type i = 0; i < other.size_; ++i) {
      const size_type index = other.physical_index(i);
      (void)emplace(other.storage_at(index));
    }
  }

  static_message_queue(static_message_queue&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
      : head_(0), tail_(0), size_(0) {
    for (size_type i = 0; i < other.size_; ++i) {
      const size_type index = other.physical_index(i);
      (void)emplace(static_cast<T&&>(other.storage_at(index)));
    }
    other.clear();
  }

  static_message_queue& operator=(const static_message_queue& other) {
    if (this == &other) {
      return *this;
    }

    clear();
    for (size_type i = 0; i < other.size_; ++i) {
      const size_type index = other.physical_index(i);
      (void)emplace(other.storage_at(index));
    }
    return *this;
  }

  static_message_queue& operator=(static_message_queue&& other) noexcept(
      std::is_nothrow_move_constructible<T>::value && std::is_nothrow_move_assignable<T>::value) {
    if (this == &other) {
      return *this;
    }

    clear();
    for (size_type i = 0; i < other.size_; ++i) {
      const size_type index = other.physical_index(i);
      (void)emplace(static_cast<T&&>(other.storage_at(index)));
    }
    other.clear();
    return *this;
  }

  METL_NODISCARD constexpr bool empty() const noexcept { return size_ == 0; }
  METL_NODISCARD constexpr bool full() const noexcept { return size_ == Capacity; }
  METL_NODISCARD constexpr size_type size() const noexcept { return size_; }
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  METL_NODISCARD reference front() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(head_);
  }

  METL_NODISCARD const_reference front() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(head_);
  }

  template <typename... Args>
  METL_NODISCARD bool try_emplace(Args&&... args) {
    if (full()) {
      return false;
    }

    new (storage_[tail_].addr()) T(std::forward<Args>(args)...);
    tail_ = advance(tail_);
    ++size_;
    return true;
  }

  template <typename... Args>
  reference emplace(Args&&... args) {
    const bool inserted = try_emplace(std::forward<Args>(args)...);
    METL_ASSERT(inserted);
    (void)inserted;
    return back_ref();
  }

  METL_NODISCARD bool try_push(const T& value) { return try_emplace(value); }
  METL_NODISCARD bool try_push(T&& value) { return try_emplace(static_cast<T&&>(value)); }

  reference push(const T& value) { return emplace(value); }
  reference push(T&& value) { return emplace(static_cast<T&&>(value)); }

  METL_NODISCARD bool try_pop(T& out) {
    if (empty()) {
      return false;
    }

    out = static_cast<T&&>(storage_at(head_));
    pop_front();
    return true;
  }

  void pop() noexcept {
    METL_ASSERT(size_ > 0);
    pop_front();
  }

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

  constexpr size_type physical_index(size_type offset) const noexcept {
    return Capacity == 0 ? 0 : (head_ + offset) % Capacity;
  }

  T& storage_at(size_type index) noexcept { return storage_[index].ref(); }
  const T& storage_at(size_type index) const noexcept { return storage_[index].ref(); }

  reference back_ref() noexcept {
    const size_type index = tail_ == 0 ? Capacity - 1 : tail_ - 1;
    return storage_at(index);
  }

  void pop_front() noexcept {
    storage_at(head_).~T();
    head_ = advance(head_);
    --size_;
  }

  storage_type storage_[Capacity == 0 ? 1 : Capacity];
  size_type head_;
  size_type tail_;
  size_type size_;
};

}  // namespace metl
