#pragma once

/// @file
/// @brief Shared ring-buffer core for `metl::ring_buffer` and `metl::fixed_deque`.
///
/// Both containers are fixed-capacity circular buffers over an inline
/// `storage_for<T>` array with `head_`/`size_` bookkeeping. `detail::ring_core`
/// holds that storage and implements every operation the two share verbatim:
/// element access (`front`/`back`/`at`/`operator[]`), back insertion
/// (`try_emplace_back`/`emplace_back`/`try_push_back`), front removal
/// (`pop_front`), `clear`, `capacity`, and value-semantic copy/move.
///
/// `ring_buffer` uses the core as-is (adding `push_overwrite`), while
/// `fixed_deque` additionally builds front insertion / back removal
/// (`emplace_front`/`pop_back`) on the protected `retreat`/`physical_index`
/// primitives. This is an internal implementation detail; there is no public
/// `metl::detail::ring_core` API contract.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {
namespace detail {

/// Circular fixed-capacity storage shared by `ring_buffer` and `fixed_deque`.
///
/// Stores up to `Capacity` elements inline; performs NO heap allocation. Not
/// thread-safe. Element order is logical: index 0 is the front (oldest / head).
///
/// @tparam T Element type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
template <typename T, std::size_t Capacity>
class ring_core {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = const T&;

  /// Constructs an empty core.
  constexpr ring_core() noexcept : head_(0), size_(0) {}

  ~ring_core() { clear(); }

  /// Copy-constructs by copying each element of `other` in order.
  ring_core(const ring_core& other) : head_(0), size_(0) {
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(other.at(i));
    }
  }

  /// Move-constructs by moving each element out of `other`, leaving it empty.
  ring_core(ring_core&& other) noexcept(std::is_nothrow_move_constructible<T>::value) : head_(0), size_(0) {
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(static_cast<T&&>(other.at(i)));
    }
    other.clear();
  }

  ring_core& operator=(const ring_core& other) {
    if (this == &other) {
      return *this;
    }

    clear();
    for (size_type i = 0; i < other.size_; ++i) {
      (void)emplace_back(other.at(i));
    }
    return *this;
  }

  ring_core& operator=(ring_core&& other) noexcept(std::is_nothrow_move_constructible<T>::value &&
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

  /// Returns the fixed capacity (`Capacity`).
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  /// Returns a reference to the front (oldest) element.
  /// @pre Non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference front() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(head_);
  }

  /// Returns a reference to the front (oldest) element.
  /// @pre Non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference front() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(head_);
  }

  /// Returns a reference to the back (newest) element.
  /// @pre Non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference back() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(physical_index(size_ - 1));
  }

  /// Returns a reference to the back (newest) element.
  /// @pre Non-empty; asserts and aborts otherwise.
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
  /// @return true on success; false if full (no assert).
  template <typename... Args>
  METL_NODISCARD bool try_emplace_back(Args&&... args) {
    if (size_ == Capacity) {
      return false;
    }

    new (storage_[physical_index(size_)].addr()) T(std::forward<Args>(args)...);
    ++size_;
    return true;
  }

  /// Constructs an element in place at the back and returns a reference to it.
  /// @pre Not full; overflow asserts and aborts.
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

  /// Removes the front (oldest) element.
  /// @pre Non-empty; asserts and aborts otherwise.
  void pop_front() noexcept {
    METL_ASSERT(size_ > 0);
    storage_at(head_).~T();
    head_ = advance(head_);
    --size_;
  }

  /// Removes all elements.
  void clear() noexcept {
    while (size_ != 0) {
      pop_front();
    }
  }

 protected:
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

}  // namespace detail
}  // namespace metl
