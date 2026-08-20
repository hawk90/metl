#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/transparent.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

/// @brief Fixed-capacity associative map kept sorted by key in a flat array.
///
/// Stores up to @c Capacity key/value pairs in place with NO heap allocation; the
/// capacity is fixed at compile time. Elements are held in ascending key order per
/// @c Compare, giving O(log n) lookup via binary search and O(n) insert/erase (shifting).
/// Not thread-safe.
///
/// @tparam Key Key type used for ordering and lookup.
/// @tparam T Mapped value type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
/// @tparam Compare Strict-weak-ordering comparator on keys (transparent comparators enable
///         heterogeneous lookup).
template <typename Key, typename T, std::size_t Capacity, typename Compare = std::less<Key>>
class flat_map {
 public:
  struct value_type {
    Key key;
    T value;
  };

  using key_type = Key;
  using mapped_type = T;
  using key_compare = Compare;
  using size_type = std::size_t;
  using reference = value_type&;
  using const_reference = const value_type&;
  using iterator = value_type*;
  using const_iterator = const value_type*;

  /// @brief Construct an empty map with a default-constructed comparator.
  constexpr flat_map() noexcept(std::is_nothrow_default_constructible_v<Compare>) : comp_(), size_(0) {}

  /// @brief Construct an empty map using the given comparator.
  explicit flat_map(const Compare& comp) noexcept(std::is_nothrow_copy_constructible_v<Compare>)
      : comp_(comp), size_(0) {}

  /// @brief Copy-construct, copying every element from @p other.
  flat_map(const flat_map& other) : comp_(other.comp_), size_(0) {
    for (const auto& item : other) {
      emplace(item.key, item.value);
    }
  }

  /// @brief Move-construct, moving elements out of @p other and leaving it empty.
  flat_map(flat_map&& other) noexcept(std::is_nothrow_move_constructible_v<value_type> &&
                                      std::is_nothrow_move_constructible_v<Compare>)
      : comp_(static_cast<Compare&&>(other.comp_)), size_(0) {
    for (auto& item : other) {
      emplace(static_cast<Key&&>(item.key), static_cast<T&&>(item.value));
    }
    other.clear();
  }

  /// @brief Destroy all contained elements.
  ~flat_map() { clear(); }

  /// @brief Copy-assign from @p other (self-assignment safe).
  flat_map& operator=(const flat_map& other) {
    if (this == &other) {
      return *this;
    }

    clear();
    comp_ = other.comp_;
    for (const auto& item : other) {
      emplace(item.key, item.value);
    }
    return *this;
  }

  /// @brief Move-assign from @p other, leaving it empty (self-assignment safe).
  flat_map& operator=(flat_map&& other) noexcept(std::is_nothrow_move_constructible_v<value_type> &&
                                                 std::is_nothrow_move_assignable_v<value_type> &&
                                                 std::is_nothrow_move_assignable_v<Compare>) {
    if (this == &other) {
      return *this;
    }

    clear();
    comp_ = static_cast<Compare&&>(other.comp_);
    for (auto& item : other) {
      emplace(static_cast<Key&&>(item.key), static_cast<T&&>(item.value));
    }
    other.clear();
    return *this;
  }

  /// @brief Iterator to the first element (elements are in ascending key order).
  METL_NODISCARD iterator begin() noexcept { return data(); }
  METL_NODISCARD const_iterator begin() const noexcept { return data(); }
  METL_NODISCARD const_iterator cbegin() const noexcept { return data(); }

  /// @brief Iterator one past the last element.
  METL_NODISCARD iterator end() noexcept { return data() + size_; }
  METL_NODISCARD const_iterator end() const noexcept { return data() + size_; }
  METL_NODISCARD const_iterator cend() const noexcept { return data() + size_; }

  /// @brief True if the map holds no elements.
  METL_NODISCARD bool empty() const noexcept { return size_ == 0; }
  /// @brief True if the map has reached its fixed capacity.
  METL_NODISCARD bool full() const noexcept { return size_ == Capacity; }
  /// @brief Current number of elements.
  METL_NODISCARD size_type size() const noexcept { return size_; }
  /// @brief Fixed maximum number of elements (the compile-time @c Capacity).
  METL_NODISCARD size_type capacity() const noexcept { return Capacity; }

  /// @brief Copy of the key comparator.
  METL_NODISCARD Compare key_comp() const noexcept(std::is_nothrow_copy_constructible_v<Compare>) {
    return comp_;
  }

  /// @brief POSITIONAL element access by 0-based index into the sorted sequence.
  /// @warning This is NOT a key lookup. Unlike @c std::map::operator[], it takes a positional
  ///          index (0..size()-1), returns the element at that position, and never inserts.
  ///          For key-based access use @c find(); for positional access prefer the
  ///          self-documenting @c nth().
  /// @param index 0-based position in ascending key order.
  /// @pre @p index < size(); a violation asserts (aborts by default), it does not throw.
  METL_NODISCARD reference operator[](size_type index) noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  METL_NODISCARD const_reference operator[](size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  /// @brief POSITIONAL element access by 0-based index into the sorted sequence.
  /// @warning This is NOT a key lookup. Unlike @c std::map::at, it takes a positional index,
  ///          not a key, and does NOT throw @c std::out_of_range. For key-based access use
  ///          @c find(); for positional access prefer @c nth().
  /// @param index 0-based position in ascending key order.
  /// @pre @p index < size(); a violation asserts (aborts by default), it does not throw.
  METL_NODISCARD reference at(size_type index) noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  METL_NODISCARD const_reference at(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  /// @brief Explicit positional accessor: the element at 0-based @p index in sorted order.
  /// @note Alias for @c operator[]/@c at; named to make the positional (non-key) intent obvious.
  /// @pre @p index < size(); a violation asserts.
  METL_NODISCARD reference nth(size_type index) noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  METL_NODISCARD const_reference nth(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  /// @brief Iterator to the first element whose key is not less than @p key.
  METL_NODISCARD iterator lower_bound(const key_type& key) noexcept {
    return begin() + lower_bound_index(key);
  }

  METL_NODISCARD const_iterator lower_bound(const key_type& key) const noexcept {
    return begin() + lower_bound_index(key);
  }

  /// @brief Iterator to the first element whose key is greater than @p key.
  METL_NODISCARD iterator upper_bound(const key_type& key) noexcept {
    return begin() + upper_bound_index(key);
  }

  METL_NODISCARD const_iterator upper_bound(const key_type& key) const noexcept {
    return begin() + upper_bound_index(key);
  }

  /// @brief Range [first, last) of elements equal to @p key (empty range if none; keys are unique).
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

  /// @brief True if an element with the given key is present.
  METL_NODISCARD bool contains(const key_type& key) const noexcept { return find(key) != nullptr; }

  /// @brief Key-based lookup: pointer to the mapped value for @p key, or @c nullptr if absent.
  /// @return Pointer to the mapped value, or @c nullptr when the key is not found.
  METL_NODISCARD mapped_type* find(const key_type& key) noexcept {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return &data()[index].value;
    }
    return nullptr;
  }

  METL_NODISCARD const mapped_type* find(const key_type& key) const noexcept {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return &data()[index].value;
    }
    return nullptr;
  }

  // ---- Heterogeneous lookup overloads (enabled when Compare is transparent) ----
  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD iterator lower_bound(const K& key) noexcept {
    return begin() + lower_bound_index(key);
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const_iterator lower_bound(const K& key) const noexcept {
    return begin() + lower_bound_index(key);
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD iterator upper_bound(const K& key) noexcept {
    return begin() + upper_bound_index(key);
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const_iterator upper_bound(const K& key) const noexcept {
    return begin() + upper_bound_index(key);
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD std::pair<iterator, iterator> equal_range(const K& key) noexcept {
    const size_type lo = lower_bound_index(key);
    const size_type hi = upper_bound_index_from(key, lo);
    return {begin() + lo, begin() + hi};
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD std::pair<const_iterator, const_iterator> equal_range(const K& key) const noexcept {
    const size_type lo = lower_bound_index(key);
    const size_type hi = upper_bound_index_from(key, lo);
    return {begin() + lo, begin() + hi};
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD bool contains(const K& key) const noexcept {
    return find(key) != nullptr;
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD mapped_type* find(const K& key) noexcept {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return &data()[index].value;
    }
    return nullptr;
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const mapped_type* find(const K& key) const noexcept {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return &data()[index].value;
    }
    return nullptr;
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  bool erase(const K& key) noexcept {
    const size_type index = lower_bound_index(key);
    if (index >= size_ || comp_(key, data()[index].key)) {
      return false;
    }

    erase_at(index);
    return true;
  }

  /// @brief Insert @p key/@p value only if @p key is absent, without overflowing.
  /// @return @c true if inserted; @c false if the key already exists OR the map is full.
  /// @note Unlike @c emplace, a full map or duplicate key is reported by the return value
  ///       rather than an assertion.
  template <typename K, typename V>
  METL_NODISCARD bool try_emplace(K&& key, V&& value) {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return false;
    }

    return try_insert_at(index, std::forward<K>(key), std::forward<V>(value));
  }

  /// @brief Insert @p key/@p value and return a reference to the new element.
  /// @return Reference to the inserted element.
  /// @pre The key is absent and the map is not full; violations assert. Use @c try_emplace to
  ///      handle a full map or duplicate key without asserting.
  template <typename K, typename V>
  reference emplace(K&& key, V&& value) {
    const size_type index = lower_bound_index(key);
    METL_ASSERT(!(index < size_ && !comp_(key, data()[index].key)));
    const bool inserted = try_insert_at(index, std::forward<K>(key), std::forward<V>(value));
    METL_ASSERT(inserted);
    (void)inserted;
    // Hard guard on the full-map path (docs/AUDIT.md, Section D): on a full map
    // try_insert_at returns false with `index == size_ == Capacity`, so the
    // return below would hand out a one-past-the-end reference. METL_ASSERT is
    // stripped at low hardening levels; METL_HARDEN never is.
    METL_HARDEN(index < size_);
    return data()[index];
  }

  /// @brief Assign @p value to an existing @p key, or insert the pair if absent.
  /// @return @c true on assign or successful insert; @c false only if a new key cannot fit (full).
  /// @note The boolean answers "did it fit", **not** std's "was it inserted rather than
  ///       assigned" — hence the @c try_ prefix, which reserves the plain name for the
  ///       asserting form below.
  template <typename K, typename V>
  METL_NODISCARD bool try_insert_or_assign(K&& key, V&& value) {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      data()[index].value = std::forward<V>(value);
      return true;
    }

    return try_insert_at(index, std::forward<K>(key), std::forward<V>(value));
  }

  /// @brief Assign @p value to an existing @p key, or insert the pair if absent.
  /// @return Reference to the assigned-to or newly inserted element.
  /// @pre A new key fits; a full map asserts. Use @c try_insert_or_assign otherwise.
  template <typename K, typename V>
  reference insert_or_assign(K&& key, V&& value) {
    const size_type index = lower_bound_index(key);
    const bool stored = try_insert_or_assign(std::forward<K>(key), std::forward<V>(value));
    METL_ASSERT(stored);
    (void)stored;
    // Same full-map hazard as emplace above: a refused insert leaves
    // `index == size_`, which would make this a one-past-the-end reference.
    METL_HARDEN(index < size_);
    return data()[index];
  }

  /// @brief Erase the element with the given key, if present.
  /// @return @c true if an element was erased; @c false if the key was not found.
  bool erase(const key_type& key) noexcept {
    const size_type index = lower_bound_index(key);
    if (index >= size_ || comp_(key, data()[index].key)) {
      return false;
    }

    erase_at(index);
    return true;
  }

  /// @brief Remove all elements (destroys each; size becomes 0).
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
      if (comp_(data()[index].key, key)) {
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
      if (!comp_(key, data()[index].key)) {
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
      if (!comp_(key, data()[index].key)) {
        first = index + 1;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return first;
  }

  template <typename K, typename V>
  METL_NODISCARD bool try_insert_at(size_type index, K&& key, V&& value) {
    if (full()) {
      return false;
    }

    shift_right_from(index);
    new (storage_[index].addr()) value_type{std::forward<K>(key), std::forward<V>(value)};
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
  // NOTE: entries live in laundered aligned storage, which is not
  // constant-evaluable, so the constexpr labels here are effective only outside
  // constant evaluation. Genuine constexpr (cf. metl::optional via
  // metl/detail/construct.hpp) would require a union-of-value_type rewrite;
  // deferred (see docs/AUDIT.md Section A).
  storage_for<value_type> storage_[Capacity == 0 ? 1 : Capacity];
  size_type size_;
};

// ---------------------------------------------------------------------------
// Relational operators
//
// Cross-capacity, like fixed_vector's and fixed_string's: two maps that hold the
// same entries compare equal whatever their declared capacities are, since
// capacity is a storage decision and not part of the value.
//
// The comparator type must match, and that restriction is deliberate rather than
// an oversight. Compare determines the ORDER the entries are stored in, so a
// flat_map<K, V, less> and a flat_map<K, V, greater> holding the same entries
// hold them in opposite sequences; a lexicographic comparison of the two would
// report a difference that says something about the comparators rather than
// about the contents.
//
// value_type is a plain aggregate with no operator== of its own, so the fields
// are compared directly here rather than through the element type.
// ---------------------------------------------------------------------------

/// @brief True when both maps hold the same entries in the same order.
template <typename Key, typename T, std::size_t N1, std::size_t N2, typename Compare>
METL_NODISCARD bool operator==(const flat_map<Key, T, N1, Compare>& lhs,
                               const flat_map<Key, T, N2, Compare>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!(lhs.begin()[i].key == rhs.begin()[i].key) || !(lhs.begin()[i].value == rhs.begin()[i].value)) {
      return false;
    }
  }
  return true;
}

/// @brief True when the maps differ in size or in any entry.
template <typename Key, typename T, std::size_t N1, std::size_t N2, typename Compare>
METL_NODISCARD bool operator!=(const flat_map<Key, T, N1, Compare>& lhs,
                               const flat_map<Key, T, N2, Compare>& rhs) {
  return !(lhs == rhs);
}

/// @brief Lexicographic order over the entry sequence, key before value.
template <typename Key, typename T, std::size_t N1, std::size_t N2, typename Compare>
METL_NODISCARD bool operator<(const flat_map<Key, T, N1, Compare>& lhs,
                              const flat_map<Key, T, N2, Compare>& rhs) {
  const std::size_t common = lhs.size() < rhs.size() ? lhs.size() : rhs.size();
  for (std::size_t i = 0; i < common; ++i) {
    if (lhs.begin()[i].key < rhs.begin()[i].key) {
      return true;
    }
    if (rhs.begin()[i].key < lhs.begin()[i].key) {
      return false;
    }
    if (lhs.begin()[i].value < rhs.begin()[i].value) {
      return true;
    }
    if (rhs.begin()[i].value < lhs.begin()[i].value) {
      return false;
    }
  }
  return lhs.size() < rhs.size();
}

/// @brief `rhs < lhs`.
template <typename Key, typename T, std::size_t N1, std::size_t N2, typename Compare>
METL_NODISCARD bool operator>(const flat_map<Key, T, N1, Compare>& lhs,
                              const flat_map<Key, T, N2, Compare>& rhs) {
  return rhs < lhs;
}

/// @brief `!(rhs < lhs)`.
template <typename Key, typename T, std::size_t N1, std::size_t N2, typename Compare>
METL_NODISCARD bool operator<=(const flat_map<Key, T, N1, Compare>& lhs,
                               const flat_map<Key, T, N2, Compare>& rhs) {
  return !(rhs < lhs);
}

/// @brief `!(lhs < rhs)`.
template <typename Key, typename T, std::size_t N1, std::size_t N2, typename Compare>
METL_NODISCARD bool operator>=(const flat_map<Key, T, N1, Compare>& lhs,
                               const flat_map<Key, T, N2, Compare>& rhs) {
  return !(lhs < rhs);
}

}  // namespace metl
