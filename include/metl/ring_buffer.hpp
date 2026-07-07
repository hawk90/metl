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
class ring_buffer {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = const T&;

  constexpr ring_buffer() noexcept : head_(0), size_(0) {}

  ~ring_buffer() { clear(); }

  ring_buffer(const ring_buffer& other) : head_(0), size_(0) {
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(other.at(i));
    }
  }

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

  METL_NODISCARD reference back() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(physical_index(size_ - 1));
  }

  METL_NODISCARD const_reference back() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(physical_index(size_ - 1));
  }

  METL_NODISCARD reference at(size_type index) noexcept {
    METL_ASSERT(index < size_);
    return storage_at(physical_index(index));
  }

  METL_NODISCARD const_reference at(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return storage_at(physical_index(index));
  }

  METL_NODISCARD reference operator[](size_type index) noexcept { return at(index); }
  METL_NODISCARD const_reference operator[](size_type index) const noexcept { return at(index); }

  template <typename... Args>
  METL_NODISCARD bool try_emplace_back(Args&&... args) {
    if (full()) {
      return false;
    }

    new (storage_[physical_index(size_)].addr()) T(std::forward<Args>(args)...);
    ++size_;
    return true;
  }

  template <typename... Args>
  reference emplace_back(Args&&... args) {
    const bool inserted = try_emplace_back(std::forward<Args>(args)...);
    METL_ASSERT(inserted);
    (void)inserted;
    return back();
  }

  METL_NODISCARD bool try_push_back(const T& value) { return try_emplace_back(value); }
  METL_NODISCARD bool try_push_back(T&& value) { return try_emplace_back(static_cast<T&&>(value)); }

  template <typename... Args>
  reference push_overwrite(Args&&... args) {
    if (full()) {
      pop_front();
    }

    return emplace_back(std::forward<Args>(args)...);
  }

  void pop_front() noexcept {
    METL_ASSERT(size_ > 0);
    storage_at(head_).~T();
    head_ = advance(head_);
    --size_;
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
