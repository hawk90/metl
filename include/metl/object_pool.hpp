#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

/// Slot-based object pool with a compile-time FIXED number of slots.
///
/// Manages up to `Capacity` objects in inline storage; performs NO heap
/// allocation. Objects are constructed in place and referred to by raw pointer;
/// pointers remain stable for the object's lifetime. Non-copyable and
/// non-movable. Not thread-safe.
///
/// @tparam T Pooled object type.
/// @tparam Capacity Number of slots (fixed at compile time).
template <typename T, std::size_t Capacity>
class object_pool {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using pointer = T*;
  using const_pointer = const T*;

  /// Constructs an empty pool with all slots free.
  constexpr object_pool() noexcept : active_{}, size_(0) {}

  ~object_pool() { clear(); }

  object_pool(const object_pool&) = delete;
  object_pool& operator=(const object_pool&) = delete;
  object_pool(object_pool&&) = delete;
  object_pool& operator=(object_pool&&) = delete;

  /// Constructs an object in the first free slot.
  /// @return Pointer to the new object, or nullptr if the pool is full (no assert).
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

  /// Constructs an object in the first free slot and returns a pointer to it.
  /// @pre Pool is not full; a full pool asserts and aborts. Use try_emplace for a
  /// non-asserting path.
  template <typename... Args>
  METL_NODISCARD pointer emplace(Args&&... args) {
    pointer object = try_emplace(std::forward<Args>(args)...);
    METL_ASSERT(object != nullptr);
    return object;
  }

  /// Destroys a pooled object and frees its slot.
  /// @param object Pointer previously returned by this pool.
  /// @return true if destroyed; false if `object` is not a live slot of this pool.
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

  /// Destroys all live objects and frees every slot.
  void clear() noexcept {
    for (size_type i = 0; i < Capacity; ++i) {
      if (active_[i]) {
        slot_ptr(i)->~T();
        active_[i] = false;
      }
    }
    size_ = 0;
  }

  /// Returns true if `object` points to a live slot of this pool.
  METL_NODISCARD bool contains(const_pointer object) const noexcept {
    const size_type index = index_of(object);
    return index < Capacity && active_[index];
  }

  /// Returns true if no slots are in use.
  METL_NODISCARD constexpr bool empty() const noexcept { return size_ == 0; }
  /// Returns true if every slot is in use.
  METL_NODISCARD constexpr bool full() const noexcept { return size_ == Capacity; }
  /// Returns the number of live objects.
  METL_NODISCARD constexpr size_type size() const noexcept { return size_; }
  /// Returns the fixed slot count (`Capacity`).
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  /// Returns the number of free slots (`Capacity - size()`).
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
