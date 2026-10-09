#pragma once

/// @file
/// @brief Progress guarantees for `metl::flat_map` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `find`, `contains` | wait-free, `floor(log2(Capacity)) + 2` comparisons |
///   | `lower_bound`, `upper_bound` | wait-free, `floor(log2(Capacity)) + 1` comparisons |
///   | `equal_range` | wait-free, `2 * floor(log2(Capacity)) + 2` comparisons |
///   | `try_emplace`, `emplace`, `insert_or_assign`, `erase` | wait-free, bounded by `Capacity` moves |
///   | `clear`, iteration, copy, destructor | wait-free, bounded by `size()` |
///
/// Storage is one sorted array, so lookup is a binary search and modification
/// shifts the tail to keep it sorted. Both bounds are compile-time known.
///
/// The `+ 2` on `find` is not slack. `lower_bound` over n elements performs
/// `floor(log2(n)) + 1` comparisons, and `find` performs one more to decide
/// whether the position it landed on actually holds the key. `equal_range` is a
/// `lower_bound` followed by an `upper_bound` searched from that position, so it
/// pays the binary search twice. `tests/containers/operation_count_test.cpp`
/// counts `find`'s comparisons and holds them to the first row; the other rows
/// are measured the same way but not gated.
///
/// Two things this header does not bound, and cannot: `Compare` must itself be
/// bounded -- a comparator that loops on the key makes every operation above
/// inherit that loop -- and so must `Key`'s and `T`'s move and destroy operations,
/// which the shifting runs `size()` times.
///
/// Single-threaded: this type does not synchronise.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/addressof.hpp"
#include "metl/detail/array_storage.hpp"
#include "metl/detail/nothrow_call.hpp"
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
  // Element-inserting constructors delegate to the empty constructor first.
  // Once it returns the object is fully constructed, so if copying or moving
  // an element throws part-way, the destructor runs and destroys exactly the
  // elements already inserted; nothing leaks.
  flat_map(const flat_map& other) : flat_map(other.comp_) {
    for (const auto& item : other) {
      emplace(item.key, item.value);
    }
  }

  /// @brief Move-construct, moving elements out of @p other and leaving it empty.
  flat_map(flat_map&& other) noexcept(std::is_nothrow_move_constructible_v<value_type> &&
                                      std::is_nothrow_move_constructible_v<Compare>)
      : flat_map(adopt_compare{}, static_cast<Compare&&>(other.comp_)) {
    // `other` is already sorted and unique, so its elements are appended in
    // order: no comparison runs, which is what makes the noexcept above true
    // for a comparator that can throw.
    append_sorted_from(other);
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
    // `other` is already sorted and unique, so its elements are appended in
    // order: no comparison runs, which is what makes the noexcept above true
    // for a comparator that can throw.
    append_sorted_from(other);
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
  METL_NODISCARD iterator lower_bound(const key_type& key) noexcept(compare_cannot_throw<key_type>) {
    return begin() + lower_bound_index(key);
  }

  METL_NODISCARD const_iterator lower_bound(const key_type& key) const
      noexcept(compare_cannot_throw<key_type>) {
    return begin() + lower_bound_index(key);
  }

  /// @brief Iterator to the first element whose key is greater than @p key.
  METL_NODISCARD iterator upper_bound(const key_type& key) noexcept(compare_cannot_throw<key_type>) {
    return begin() + upper_bound_index(key);
  }

  METL_NODISCARD const_iterator upper_bound(const key_type& key) const
      noexcept(compare_cannot_throw<key_type>) {
    return begin() + upper_bound_index(key);
  }

  /// @brief Range [first, last) of elements equal to @p key (empty range if none; keys are unique).
  METL_NODISCARD std::pair<iterator, iterator> equal_range(const key_type& key) noexcept(
      compare_cannot_throw<key_type>) {
    const size_type lo = lower_bound_index(key);
    const size_type hi = upper_bound_index_from(key, lo);
    return {begin() + lo, begin() + hi};
  }

  METL_NODISCARD std::pair<const_iterator, const_iterator> equal_range(const key_type& key) const
      noexcept(compare_cannot_throw<key_type>) {
    const size_type lo = lower_bound_index(key);
    const size_type hi = upper_bound_index_from(key, lo);
    return {begin() + lo, begin() + hi};
  }

  /// @brief True if an element with the given key is present.
  METL_NODISCARD bool contains(const key_type& key) const noexcept(compare_cannot_throw<key_type>) {
    return find(key) != nullptr;
  }

  /// @brief Key-based lookup: pointer to the mapped value for @p key, or @c nullptr if absent.
  /// @return Pointer to the mapped value, or @c nullptr when the key is not found.
  METL_NODISCARD mapped_type* find(const key_type& key) noexcept(compare_cannot_throw<key_type>) {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return detail::addressof(data()[index].value);
    }
    return nullptr;
  }

  METL_NODISCARD const mapped_type* find(const key_type& key) const noexcept(compare_cannot_throw<key_type>) {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return detail::addressof(data()[index].value);
    }
    return nullptr;
  }

  // ---- Heterogeneous lookup overloads (enabled when Compare is transparent) ----
  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD iterator lower_bound(const K& key) noexcept(compare_cannot_throw<K>) {
    return begin() + lower_bound_index(key);
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const_iterator lower_bound(const K& key) const noexcept(compare_cannot_throw<K>) {
    return begin() + lower_bound_index(key);
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD iterator upper_bound(const K& key) noexcept(compare_cannot_throw<K>) {
    return begin() + upper_bound_index(key);
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const_iterator upper_bound(const K& key) const noexcept(compare_cannot_throw<K>) {
    return begin() + upper_bound_index(key);
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD std::pair<iterator, iterator> equal_range(const K& key) noexcept(compare_cannot_throw<K>) {
    const size_type lo = lower_bound_index(key);
    const size_type hi = upper_bound_index_from(key, lo);
    return {begin() + lo, begin() + hi};
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD std::pair<const_iterator, const_iterator> equal_range(const K& key) const
      noexcept(compare_cannot_throw<K>) {
    const size_type lo = lower_bound_index(key);
    const size_type hi = upper_bound_index_from(key, lo);
    return {begin() + lo, begin() + hi};
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD bool contains(const K& key) const noexcept(compare_cannot_throw<K>) {
    return find(key) != nullptr;
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD mapped_type* find(const K& key) noexcept(compare_cannot_throw<K>) {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return detail::addressof(data()[index].value);
    }
    return nullptr;
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const mapped_type* find(const K& key) const noexcept(compare_cannot_throw<K>) {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return detail::addressof(data()[index].value);
    }
    return nullptr;
  }

  template <
      typename K,
      typename = enable_if_t<detail::has_is_transparent_v<Compare> && !std::is_same_v<decay_t<K>, key_type>>>
  bool erase(const K& key) noexcept(compare_cannot_throw<K> && relocate_cannot_throw) {
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
    if constexpr (!std::is_same_v<std::decay_t<K>, key_type>) {
      // Convert first, so the position, the duplicate check and the stored
      // key all come from the same value; a narrowing conversion would
      // otherwise store a key that does not belong where the argument put it.
      return try_emplace(key_type{std::forward<K>(key)}, std::forward<V>(value));
    }
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
    if constexpr (!std::is_same_v<std::decay_t<K>, key_type>) {
      // Convert first, so the position, the duplicate check and the stored
      // key all come from the same value; a narrowing conversion would
      // otherwise store a key that does not belong where the argument put it.
      return emplace(key_type{std::forward<K>(key)}, std::forward<V>(value));
    }
    const size_type index = lower_bound_index(key);
    METL_HARDEN(!(index < size_ && !comp_(key, data()[index].key)));
    const bool inserted = try_insert_at(index, std::forward<K>(key), std::forward<V>(value));
    METL_ASSERT(inserted);
    (void)inserted;
    // Hard guard on the full-map path: on a full map
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
    if constexpr (!std::is_same_v<std::decay_t<K>, key_type>) {
      // Convert first, so the position, the duplicate check and the stored
      // key all come from the same value; a narrowing conversion would
      // otherwise store a key that does not belong where the argument put it.
      return try_insert_or_assign(key_type{std::forward<K>(key)}, std::forward<V>(value));
    }
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
    if constexpr (!std::is_same_v<std::decay_t<K>, key_type>) {
      // Convert first, so the position, the duplicate check and the stored
      // key all come from the same value; a narrowing conversion would
      // otherwise store a key that does not belong where the argument put it.
      return insert_or_assign(key_type{std::forward<K>(key)}, std::forward<V>(value));
    }
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
  bool erase(const key_type& key) noexcept(compare_cannot_throw<key_type> && relocate_cannot_throw) {
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
  // The lookups call the comparator in both argument orders; they are noexcept
  // exactly when it cannot throw. See detail/nothrow_call.hpp
  // for why std::less is looked through rather than taken at its word.
  template <typename K>
  static constexpr bool compare_cannot_throw =
      detail::nothrow_binary_call_v<Compare, const K&, const key_type&> &&
      detail::nothrow_binary_call_v<Compare, const key_type&, const K&>;
  // erase relocates the tail by move construction.
  static constexpr bool relocate_cannot_throw = std::is_nothrow_move_constructible_v<value_type>;

  // Moves every element of `other` (sorted, unique) onto the end of this empty
  // container, in order.
  void append_sorted_from(flat_map& other) noexcept(std::is_nothrow_move_constructible_v<value_type>) {
    for (auto& item : other) {
      ::new (storage_.slot(size_)) value_type(static_cast<value_type&&>(item));
      ++size_;
    }
  }
  // Empty map that MOVES its comparator in; the move constructor delegates
  // here (the public comparator constructor copies).
  struct adopt_compare {};
  flat_map(adopt_compare, Compare&& comp) noexcept(std::is_nothrow_move_constructible_v<Compare>)
      : comp_(static_cast<Compare&&>(comp)), size_(0) {}

  value_type* data() noexcept { return storage_.data(); }
  const value_type* data() const noexcept { return storage_.data(); }

  template <typename K>
  size_type lower_bound_index(const K& key) const noexcept(compare_cannot_throw<K>) {
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
  size_type upper_bound_index(const K& key) const noexcept(compare_cannot_throw<K>) {
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
  size_type upper_bound_index_from(const K& key, size_type lo) const noexcept(compare_cannot_throw<K>) {
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

    // Build the entry before shifting: `value` may refer to a mapped value in
    // this map (`m.try_emplace(k, m.nth(0).value)`), which the shift moves from.
    value_type entry{std::forward<K>(key), std::forward<V>(value)};
    insert_shifting(index, static_cast<value_type&&>(entry));
    return true;
  }

  // Inserts `entry` at `index`, shifting [index, size_) right by one, the way
  // std::vector::insert does: move-construct the new last slot, then
  // move-ASSIGN the rest backwards. Every slot in [0, size_) stays a live
  // object throughout, so a throwing move cannot leave a destroyed slot inside
  // the range for the destructor to destroy a second time (a
  // construct-then-destroy loop would). A throw part-way
  // would still leave the order broken, so -- as std::flat_map does -- the
  // container is cleared to restore its invariant before rethrowing.
  void insert_shifting(size_type index, value_type&& entry) {
    if constexpr (std::is_move_assignable_v<value_type>) {
      if (index == size_) {
        ::new (storage_.slot(size_)) value_type(static_cast<value_type&&>(entry));
        ++size_;
        return;
      }
      ::new (storage_.slot(size_)) value_type(static_cast<value_type&&>(data()[size_ - 1]));
      ++size_;
#if !METL_NO_EXCEPTIONS
      try {
#endif
        for (size_type i = size_ - 2; i > index; --i) {
          data()[i] = static_cast<value_type&&>(data()[i - 1]);
        }
        data()[index] = static_cast<value_type&&>(entry);
#if !METL_NO_EXCEPTIONS
      } catch (...) {
        clear();
        throw;
      }
#endif
    } else {
      // A non-assignable element can only be relocated by construct+destroy.
      shift_right_from(index);
      ::new (storage_.slot(index)) value_type(static_cast<value_type&&>(entry));
      ++size_;
    }
  }

  // Relocates [index, size_) one slot right by construct-then-destroy, for an
  // element that cannot be move-assigned. A throw while constructing slot `i`
  // leaves [0, i) live, slot i dead and (i, size_] holding relocated elements;
  // the handler destroys exactly those and empties the container, the same
  // outcome as the assignable path. Slot i is skipped: it is already dead, and
  // destroying it again would be a double destroy.
  void shift_right_from(size_type index) {
    size_type i = size_;
#if !METL_NO_EXCEPTIONS
    try {
#endif
      for (; i > index; --i) {
        ::new (storage_.slot(i)) value_type(static_cast<value_type&&>(data()[i - 1]));
        data()[i - 1].~value_type();
      }
#if !METL_NO_EXCEPTIONS
    } catch (...) {
      for (size_type j = 0; j < i; ++j) {
        data()[j].~value_type();
      }
      for (size_type j = i + 1; j <= size_; ++j) {
        data()[j].~value_type();
      }
      size_ = 0;
      throw;
    }
#endif
  }

  // Closes the gap at `index` by move-constructing each later element one slot
  // down. If such a move can throw, a throw at slot i leaves [0, i) live, slot i
  // dead and (i, size_) live; the handler destroys exactly those and empties the
  // container -- the same outcome as a throwing insert. The noexcept is
  // conditional so that throw propagates instead of terminating.
  void erase_at(size_type index) noexcept(relocate_cannot_throw) {
    data()[index].~value_type();
    if constexpr (relocate_cannot_throw) {
      for (size_type i = index; i + 1 < size_; ++i) {
        ::new (storage_.slot(i)) value_type(static_cast<value_type&&>(data()[i + 1]));
        data()[i + 1].~value_type();
      }
    } else {
      size_type i = index;
#if !METL_NO_EXCEPTIONS
      try {
#endif
        for (; i + 1 < size_; ++i) {
          ::new (storage_.slot(i)) value_type(static_cast<value_type&&>(data()[i + 1]));
          data()[i + 1].~value_type();
        }
#if !METL_NO_EXCEPTIONS
      } catch (...) {
        for (size_type j = 0; j < i; ++j) {
          data()[j].~value_type();
        }
        for (size_type j = i + 1; j < size_; ++j) {
          data()[j].~value_type();
        }
        size_ = 0;
        throw;
      }
#endif
    }
    --size_;
  }

  Compare comp_;
  // Entries live in one aligned byte buffer reached as a single
  // value_type[Capacity] array object, so data() + i is in-array arithmetic
  // (the reasoning is in detail/array_storage.hpp).
  // array_storage::data() goes through a reinterpret_cast, which is never
  // constant-evaluable, so the constexpr labels here are effective only outside
  // constant evaluation.
  detail::array_storage<value_type, (Capacity == 0 ? 1 : Capacity)> storage_;
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
