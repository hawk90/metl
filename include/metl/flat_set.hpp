#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

namespace detail {

template <typename T, typename = void>
struct flat_set_has_transparent_compare : false_type {};

template <typename T>
struct flat_set_has_transparent_compare<T, void_t<typename T::is_transparent>> : true_type {};

template <typename T>
inline constexpr bool flat_set_has_transparent_compare_v = flat_set_has_transparent_compare<T>::value;

}  // namespace detail

template <typename Key, std::size_t Capacity, typename Compare = std::less<Key>>
class flat_set {
 public:
  using key_type = Key;
  using value_type = Key;
  using key_compare = Compare;
  using value_compare = Compare;
  using size_type = std::size_t;
  using reference = value_type&;
  using const_reference = const value_type&;
  using iterator = value_type*;
  using const_iterator = const value_type*;

  constexpr flat_set() noexcept(std::is_nothrow_default_constructible<Compare>::value) : comp_(), size_(0) {}

  explicit flat_set(const Compare& comp) noexcept(std::is_nothrow_copy_constructible<Compare>::value)
      : comp_(comp), size_(0) {}

  flat_set(const flat_set& other) : comp_(other.comp_), size_(0) {
    for (const auto& item : other) {
      emplace(item);
    }
  }

  flat_set(flat_set&& other) noexcept(std::is_nothrow_move_constructible<value_type>::value &&
                                      std::is_nothrow_move_constructible<Compare>::value)
      : comp_(static_cast<Compare&&>(other.comp_)), size_(0) {
    for (auto& item : other) {
      emplace(static_cast<Key&&>(item));
    }
    other.clear();
  }

  ~flat_set() { clear(); }

  flat_set& operator=(const flat_set& other) {
    if (this == &other) {
      return *this;
    }

    clear();
    comp_ = other.comp_;
    for (const auto& item : other) {
      emplace(item);
    }
    return *this;
  }

  flat_set& operator=(flat_set&& other) noexcept(std::is_nothrow_move_constructible<value_type>::value &&
                                                 std::is_nothrow_move_assignable<value_type>::value &&
                                                 std::is_nothrow_move_assignable<Compare>::value) {
    if (this == &other) {
      return *this;
    }

    clear();
    comp_ = static_cast<Compare&&>(other.comp_);
    for (auto& item : other) {
      emplace(static_cast<Key&&>(item));
    }
    other.clear();
    return *this;
  }

  METL_NODISCARD iterator begin() noexcept { return data(); }
  METL_NODISCARD const_iterator begin() const noexcept { return data(); }
  METL_NODISCARD const_iterator cbegin() const noexcept { return data(); }

  METL_NODISCARD iterator end() noexcept { return data() + size_; }
  METL_NODISCARD const_iterator end() const noexcept { return data() + size_; }
  METL_NODISCARD const_iterator cend() const noexcept { return data() + size_; }

  METL_NODISCARD bool empty() const noexcept { return size_ == 0; }
  METL_NODISCARD bool full() const noexcept { return size_ == Capacity; }
  METL_NODISCARD size_type size() const noexcept { return size_; }
  METL_NODISCARD size_type capacity() const noexcept { return Capacity; }

  METL_NODISCARD Compare key_comp() const noexcept(std::is_nothrow_copy_constructible<Compare>::value) {
    return comp_;
  }

  METL_NODISCARD Compare value_comp() const noexcept(std::is_nothrow_copy_constructible<Compare>::value) {
    return comp_;
  }

  // DIVERGENCE FROM std::set: operator[] and at() here are POSITIONAL. They
  // take a 0-based index into the sorted sequence and return the element at
  // that position (asserting the index is in range); they are NOT key lookups.
  // For membership/lookup use find()/contains()/lower_bound(). nth() is an
  // explicit, self-documenting alias for this positional access.
  METL_NODISCARD reference operator[](size_type index) noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  METL_NODISCARD const_reference operator[](size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  // Positional accessor (see operator[]). Asserts the index is in range; unlike
  // std::set it does not throw and is not key-based.
  METL_NODISCARD reference at(size_type index) noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  METL_NODISCARD const_reference at(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  // Explicit positional accessor: the element at 0-based `index` in sorted
  // order. Alias for operator[]/at; named to make positional intent obvious.
  METL_NODISCARD reference nth(size_type index) noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  METL_NODISCARD const_reference nth(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  METL_NODISCARD iterator lower_bound(const key_type& key) noexcept {
    return begin() + lower_bound_index(key);
  }

  METL_NODISCARD const_iterator lower_bound(const key_type& key) const noexcept {
    return begin() + lower_bound_index(key);
  }

  METL_NODISCARD iterator upper_bound(const key_type& key) noexcept {
    return begin() + upper_bound_index(key);
  }

  METL_NODISCARD const_iterator upper_bound(const key_type& key) const noexcept {
    return begin() + upper_bound_index(key);
  }

  METL_NODISCARD std::pair<iterator, iterator> equal_range(const key_type& key) noexcept {
    const size_type lo = lower_bound_index(key);
    const size_type hi = upper_bound_index_from(key, lo);
    return {begin() + lo, begin() + hi};
  }

  METL_NODISCARD std::pair<const_iterator, const_iterator> equal_range(const key_type& key) const noexcept {
    const size_type lo = lower_bound_index(key);
    const size_type hi = upper_bound_index_from(key, lo);
    return {begin() + lo, begin() + hi};
  }

  METL_NODISCARD bool contains(const key_type& key) const noexcept { return find(key) != nullptr; }

  METL_NODISCARD value_type* find(const key_type& key) noexcept {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index])) {
      return &data()[index];
    }
    return nullptr;
  }

  METL_NODISCARD const value_type* find(const key_type& key) const noexcept {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index])) {
      return &data()[index];
    }
    return nullptr;
  }

  // ---- Heterogeneous lookup overloads (enabled when Compare is transparent) ----
  template <typename K,
            typename = enable_if_t<detail::flat_set_has_transparent_compare_v<Compare> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD iterator lower_bound(const K& key) noexcept {
    return begin() + lower_bound_index(key);
  }

  template <typename K,
            typename = enable_if_t<detail::flat_set_has_transparent_compare_v<Compare> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD const_iterator lower_bound(const K& key) const noexcept {
    return begin() + lower_bound_index(key);
  }

  template <typename K,
            typename = enable_if_t<detail::flat_set_has_transparent_compare_v<Compare> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD iterator upper_bound(const K& key) noexcept {
    return begin() + upper_bound_index(key);
  }

  template <typename K,
            typename = enable_if_t<detail::flat_set_has_transparent_compare_v<Compare> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD const_iterator upper_bound(const K& key) const noexcept {
    return begin() + upper_bound_index(key);
  }

  template <typename K,
            typename = enable_if_t<detail::flat_set_has_transparent_compare_v<Compare> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD std::pair<iterator, iterator> equal_range(const K& key) noexcept {
    const size_type lo = lower_bound_index(key);
    const size_type hi = upper_bound_index_from(key, lo);
    return {begin() + lo, begin() + hi};
  }

  template <typename K,
            typename = enable_if_t<detail::flat_set_has_transparent_compare_v<Compare> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD std::pair<const_iterator, const_iterator> equal_range(const K& key) const noexcept {
    const size_type lo = lower_bound_index(key);
    const size_type hi = upper_bound_index_from(key, lo);
    return {begin() + lo, begin() + hi};
  }

  template <typename K,
            typename = enable_if_t<detail::flat_set_has_transparent_compare_v<Compare> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD bool contains(const K& key) const noexcept {
    return find(key) != nullptr;
  }

  template <typename K,
            typename = enable_if_t<detail::flat_set_has_transparent_compare_v<Compare> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD value_type* find(const K& key) noexcept {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index])) {
      return &data()[index];
    }
    return nullptr;
  }

  template <typename K,
            typename = enable_if_t<detail::flat_set_has_transparent_compare_v<Compare> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD const value_type* find(const K& key) const noexcept {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index])) {
      return &data()[index];
    }
    return nullptr;
  }

  template <typename K,
            typename = enable_if_t<detail::flat_set_has_transparent_compare_v<Compare> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  bool erase(const K& key) noexcept {
    const size_type index = lower_bound_index(key);
    if (index >= size_ || comp_(key, data()[index])) {
      return false;
    }

    erase_at(index);
    return true;
  }

  template <typename K>
  bool try_emplace(K&& key) {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index])) {
      return false;
    }

    return try_insert_at(index, std::forward<K>(key));
  }

  template <typename K>
  reference emplace(K&& key) {
    const size_type index = lower_bound_index(key);
    METL_ASSERT(!(index < size_ && !comp_(key, data()[index])));
    const bool inserted = try_insert_at(index, std::forward<K>(key));
    METL_ASSERT(inserted);
    (void)inserted;
    return data()[index];
  }

  bool erase(const key_type& key) noexcept {
    const size_type index = lower_bound_index(key);
    if (index >= size_ || comp_(key, data()[index])) {
      return false;
    }

    erase_at(index);
    return true;
  }

  void clear() noexcept {
    while (size_ > 0) {
      erase_at(size_ - 1);
    }
  }

 private:
  value_type* data() noexcept { return std::launder(reinterpret_cast<value_type*>(storage_[0].addr())); }
  const value_type* data() const noexcept {
    return std::launder(reinterpret_cast<const value_type*>(storage_[0].addr()));
  }

  template <typename K>
  size_type lower_bound_index(const K& key) const noexcept {
    size_type first = 0;
    size_type count = size_;
    while (count > 0) {
      const size_type step = count / 2;
      const size_type index = first + step;
      if (comp_(data()[index], key)) {
        first = index + 1;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return first;
  }

  template <typename K>
  size_type upper_bound_index(const K& key) const noexcept {
    size_type first = 0;
    size_type count = size_;
    while (count > 0) {
      const size_type step = count / 2;
      const size_type index = first + step;
      if (!comp_(key, data()[index])) {
        first = index + 1;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return first;
  }

  template <typename K>
  size_type upper_bound_index_from(const K& key, size_type lo) const noexcept {
    size_type first = lo;
    size_type count = size_ - lo;
    while (count > 0) {
      const size_type step = count / 2;
      const size_type index = first + step;
      if (!comp_(key, data()[index])) {
        first = index + 1;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return first;
  }

  template <typename K>
  bool try_insert_at(size_type index, K&& key) {
    if (full()) {
      return false;
    }

    shift_right_from(index);
    new (storage_[index].addr()) value_type(std::forward<K>(key));
    ++size_;
    return true;
  }

  void shift_right_from(size_type index) {
    for (size_type i = size_; i > index; --i) {
      new (storage_[i].addr()) value_type(static_cast<value_type&&>(data()[i - 1]));
      data()[i - 1].~value_type();
    }
  }

  void erase_at(size_type index) noexcept {
    data()[index].~value_type();
    for (size_type i = index; i + 1 < size_; ++i) {
      new (storage_[i].addr()) value_type(static_cast<value_type&&>(data()[i + 1]));
      data()[i + 1].~value_type();
    }
    --size_;
  }

  Compare comp_;
  storage_for<value_type> storage_[Capacity == 0 ? 1 : Capacity];
  size_type size_;
};

}  // namespace metl
