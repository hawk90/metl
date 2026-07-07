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
class object_pool {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using pointer = T*;
  using const_pointer = const T*;

  constexpr object_pool() noexcept : active_{}, size_(0) {}

  ~object_pool() { clear(); }

  object_pool(const object_pool&) = delete;
  object_pool& operator=(const object_pool&) = delete;
  object_pool(object_pool&&) = delete;
  object_pool& operator=(object_pool&&) = delete;

  template <typename... Args>
  METL_NODISCARD pointer try_emplace(Args&&... args) {
    for (size_type i = 0; i < Capacity; ++i) {
      if (!active_[i]) {
        new (storage_[i].addr()) T(std::forward<Args>(args)...);
        active_[i] = true;
        ++size_;
        return slot_ptr(i);
      }
    }

    return nullptr;
  }

  template <typename... Args>
  METL_NODISCARD pointer emplace(Args&&... args) {
    pointer object = try_emplace(std::forward<Args>(args)...);
    METL_ASSERT(object != nullptr);
    return object;
  }

  bool destroy(pointer object) noexcept {
    const size_type index = index_of(object);
    if (index >= Capacity || !active_[index]) {
      return false;
    }

    slot_ptr(index)->~T();
    active_[index] = false;
    --size_;
    return true;
  }

  void clear() noexcept {
    for (size_type i = 0; i < Capacity; ++i) {
      if (active_[i]) {
        slot_ptr(i)->~T();
        active_[i] = false;
      }
    }
    size_ = 0;
  }

  METL_NODISCARD bool contains(const_pointer object) const noexcept {
    const size_type index = index_of(object);
    return index < Capacity && active_[index];
  }

  METL_NODISCARD constexpr bool empty() const noexcept { return size_ == 0; }
  METL_NODISCARD constexpr bool full() const noexcept { return size_ == Capacity; }
  METL_NODISCARD constexpr size_type size() const noexcept { return size_; }
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  METL_NODISCARD constexpr size_type available() const noexcept { return Capacity - size_; }

 private:
  using storage_type = storage_for<T>;

  pointer slot_ptr(size_type index) noexcept { return storage_[index].ptr(); }
  const_pointer slot_ptr(size_type index) const noexcept { return storage_[index].ptr(); }

  size_type index_of(const_pointer object) const noexcept {
    if (object == nullptr) {
      return Capacity;
    }

    const_pointer begin = slot_ptr(0);
    const_pointer end = begin + Capacity;
    if (object < begin || object >= end) {
      return Capacity;
    }

    return static_cast<size_type>(object - begin);
  }

  storage_type storage_[Capacity == 0 ? 1 : Capacity];
  bool active_[Capacity == 0 ? 1 : Capacity];
  size_type size_;
};

}  // namespace metl
