#pragma once

#include "metl/bit.hpp"
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
struct has_transparent_key_eq : false_type {};

template <typename T>
struct has_transparent_key_eq<T, void_t<typename T::is_transparent>> : true_type {};

template <typename T>
inline constexpr bool has_transparent_key_eq_v = has_transparent_key_eq<T>::value;

template <typename T, typename = void>
struct has_transparent_hash : false_type {};

template <typename T>
struct has_transparent_hash<T, void_t<typename T::is_transparent>> : true_type {};

template <typename T>
inline constexpr bool has_transparent_hash_v = has_transparent_hash<T>::value;

template <typename Hash, typename KeyEqual>
inline constexpr bool is_transparent_v = has_transparent_key_eq_v<KeyEqual> && has_transparent_hash_v<Hash>;

// Compute the bucket_count from a user-requested Capacity.
// For Capacity > 0 we pick the smallest power of two that is >= Capacity * 2 (and >= 2).
// This keeps load factor <= 50% and enables `hash & (bucket_count - 1)` instead of `% bucket_count`
// (no hardware divide on ARM Cortex-M).
constexpr std::size_t compute_bucket_count(std::size_t capacity) noexcept {
  return capacity == 0 ? 1 : bit_ceil(capacity * 2);
}

}  // namespace detail

/// @brief Fixed-capacity hash map using open addressing with linear probing.
///
/// Holds up to @c Capacity key/value pairs in place with NO heap allocation; capacity is fixed
/// at compile time. The bucket table is a power of two sized so probing uses a mask instead of
/// modulo; erased slots leave tombstones. Iteration order is unspecified. Not thread-safe.
///
/// @tparam Key Key type.
/// @tparam T Mapped value type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
/// @tparam Hash Hash functor for keys (a transparent hasher plus transparent @c KeyEqual enables
///         heterogeneous lookup).
/// @tparam KeyEqual Equality comparator for keys.
template <typename Key,
          typename T,
          std::size_t Capacity,
          typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class static_unordered_map {
 public:
  struct value_type {
    Key key;
    T value;
  };

  using key_type = Key;
  using mapped_type = T;
  using size_type = std::size_t;
  using reference = value_type&;
  using const_reference = const value_type&;

  /// @brief Number of hash buckets (always a power of two, >= 2*Capacity).
  /// @note Computed from @c Capacity so probing can use `index & (bucket_count - 1)` instead of
  ///       modulo. This is the table size, larger than @c Capacity (the element ceiling).
  static constexpr size_type bucket_count = detail::compute_bucket_count(Capacity);
  static_assert((bucket_count & (bucket_count - 1)) == 0, "bucket_count must be a power of two");

 private:
  static constexpr size_type npos = static_cast<size_type>(-1);

  enum class slot_state : unsigned char {
    empty = 0,
    occupied = 1,
    tombstone = 2,
  };

 public:
  /// @brief Forward iterator over occupied slots (skips empty and tombstone slots).
  class iterator {
   public:
    using difference_type = std::ptrdiff_t;
    using value_type = static_unordered_map::value_type;
    using pointer = value_type*;
    using reference = value_type&;
    using iterator_category = std::forward_iterator_tag;

    iterator() noexcept : map_(nullptr), index_(0) {}

    reference operator*() const noexcept { return *map_->slot_value(index_); }
    pointer operator->() const noexcept { return &(**this); }

    iterator& operator++() noexcept {
      ++index_;
      skip_to_occupied();
      return *this;
    }

    iterator operator++(int) noexcept {
      iterator copy(*this);
      ++(*this);
      return copy;
    }

    friend bool operator==(const iterator& lhs, const iterator& rhs) noexcept {
      return lhs.map_ == rhs.map_ && lhs.index_ == rhs.index_;
    }

    friend bool operator!=(const iterator& lhs, const iterator& rhs) noexcept { return !(lhs == rhs); }

   private:
    friend class static_unordered_map;

    iterator(static_unordered_map* map, size_type index) noexcept : map_(map), index_(index) {
      skip_to_occupied();
    }

    void skip_to_occupied() noexcept {
      if (map_ == nullptr) {
        return;
      }

      while (index_ < bucket_count && map_->states_[index_] != slot_state::occupied) {
        ++index_;
      }
    }

    static_unordered_map* map_;
    size_type index_;
  };

  /// @brief Const forward iterator over occupied slots (skips empty and tombstone slots).
  class const_iterator {
   public:
    using difference_type = std::ptrdiff_t;
    using value_type = static_unordered_map::value_type;
    using pointer = const value_type*;
    using reference = const value_type&;
    using iterator_category = std::forward_iterator_tag;

    const_iterator() noexcept : map_(nullptr), index_(0) {}
    const_iterator(iterator other) noexcept : map_(other.map_), index_(other.index_) {}

    reference operator*() const noexcept { return *map_->slot_value(index_); }
    pointer operator->() const noexcept { return &(**this); }

    const_iterator& operator++() noexcept {
      ++index_;
      skip_to_occupied();
      return *this;
    }

    const_iterator operator++(int) noexcept {
      const_iterator copy(*this);
      ++(*this);
      return copy;
    }

    friend bool operator==(const const_iterator& lhs, const const_iterator& rhs) noexcept {
      return lhs.map_ == rhs.map_ && lhs.index_ == rhs.index_;
    }

    friend bool operator!=(const const_iterator& lhs, const const_iterator& rhs) noexcept {
      return !(lhs == rhs);
    }

   private:
    friend class static_unordered_map;

    const_iterator(const static_unordered_map* map, size_type index) noexcept : map_(map), index_(index) {
      skip_to_occupied();
    }

    void skip_to_occupied() noexcept {
      if (map_ == nullptr) {
        return;
      }

      while (index_ < bucket_count && map_->states_[index_] != slot_state::occupied) {
        ++index_;
      }
    }

    const static_unordered_map* map_;
    size_type index_;
  };

  /// @brief Construct an empty map with all slots marked empty.
  static_unordered_map() noexcept : size_(0), hasher_(), key_equal_() { initialize_states(); }

  /// @brief Copy-construct, re-inserting every element from @p other.
  static_unordered_map(const static_unordered_map& other)
      : size_(0), hasher_(other.hasher_), key_equal_(other.key_equal_) {
    initialize_states();
    for (const auto& item : other) {
      emplace(item.key, item.value);
    }
  }

  /// @brief Move-construct, moving elements out of @p other and leaving it empty.
  static_unordered_map(static_unordered_map&& other) noexcept(
      std::is_nothrow_move_constructible<value_type>::value)
      : size_(0),
        hasher_(static_cast<Hash&&>(other.hasher_)),
        key_equal_(static_cast<KeyEqual&&>(other.key_equal_)) {
    initialize_states();
    for (auto& item : other) {
      emplace(static_cast<Key&&>(item.key), static_cast<T&&>(item.value));
    }
    other.clear();
  }

  /// @brief Destroy all contained elements.
  ~static_unordered_map() { clear(); }

  /// @brief Copy-assign from @p other (self-assignment safe).
  static_unordered_map& operator=(const static_unordered_map& other) {
    if (this == &other) {
      return *this;
    }

    clear();
    hasher_ = other.hasher_;
    key_equal_ = other.key_equal_;
    for (const auto& item : other) {
      emplace(item.key, item.value);
    }
    return *this;
  }

  /// @brief Move-assign from @p other, leaving it empty (self-assignment safe).
  static_unordered_map& operator=(static_unordered_map&& other) noexcept(
      std::is_nothrow_move_constructible<value_type>::value && std::is_nothrow_move_assignable<Hash>::value &&
      std::is_nothrow_move_assignable<KeyEqual>::value) {
    if (this == &other) {
      return *this;
    }

    clear();
    hasher_ = static_cast<Hash&&>(other.hasher_);
    key_equal_ = static_cast<KeyEqual&&>(other.key_equal_);
    for (auto& item : other) {
      emplace(static_cast<Key&&>(item.key), static_cast<T&&>(item.value));
    }
    other.clear();
    return *this;
  }

  /// @brief Iterator to the first occupied slot (iteration order is unspecified).
  METL_NODISCARD iterator begin() noexcept { return iterator(this, 0); }
  METL_NODISCARD const_iterator begin() const noexcept { return const_iterator(this, 0); }
  METL_NODISCARD const_iterator cbegin() const noexcept { return const_iterator(this, 0); }

  /// @brief Past-the-end iterator.
  METL_NODISCARD iterator end() noexcept { return iterator(this, bucket_count); }
  METL_NODISCARD const_iterator end() const noexcept { return const_iterator(this, bucket_count); }
  METL_NODISCARD const_iterator cend() const noexcept { return const_iterator(this, bucket_count); }

  /// @brief True if the map holds no elements.
  METL_NODISCARD bool empty() const noexcept { return size_ == 0; }
  /// @brief True if the map has reached its fixed capacity.
  METL_NODISCARD bool full() const noexcept { return size_ == Capacity; }
  /// @brief Current number of elements.
  METL_NODISCARD size_type size() const noexcept { return size_; }
  /// @brief Fixed maximum number of elements (the compile-time @c Capacity).
  METL_NODISCARD size_type capacity() const noexcept { return Capacity; }

  /// @brief True if an element with the given key is present.
  METL_NODISCARD bool contains(const key_type& key) const noexcept { return find(key) != nullptr; }

  /// @brief Key lookup: pointer to the mapped value for @p key, or @c nullptr if absent.
  /// @return Pointer to the mapped value, or @c nullptr when the key is not found.
  METL_NODISCARD mapped_type* find(const key_type& key) noexcept {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : &slot_value(index)->value;
  }

  METL_NODISCARD const mapped_type* find(const key_type& key) const noexcept {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : &slot_value(index)->value;
  }

  /// @brief Key lookup returning an iterator, or @c end() if the key is absent.
  METL_NODISCARD iterator find_iterator(const key_type& key) noexcept {
    const size_type index = find_existing_index(key);
    return iterator(this, index == npos ? bucket_count : index);
  }

  METL_NODISCARD const_iterator find_iterator(const key_type& key) const noexcept {
    const size_type index = find_existing_index(key);
    return const_iterator(this, index == npos ? bucket_count : index);
  }

  /// @brief STL-compatible iterator-returning find (alias for @c find_iterator).
  METL_NODISCARD iterator find_iter(const key_type& key) noexcept { return find_iterator(key); }
  METL_NODISCARD const_iterator find_iter(const key_type& key) const noexcept { return find_iterator(key); }

  // ---- Heterogeneous lookup overloads (enabled when both Hash and KeyEqual are transparent) ----
  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD bool contains(const K& key) const noexcept {
    return find_existing_index(key) != npos;
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD mapped_type* find(const K& key) noexcept {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : &slot_value(index)->value;
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD const mapped_type* find(const K& key) const noexcept {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : &slot_value(index)->value;
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD iterator find_iterator(const K& key) noexcept {
    const size_type index = find_existing_index(key);
    return iterator(this, index == npos ? bucket_count : index);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD const_iterator find_iterator(const K& key) const noexcept {
    const size_type index = find_existing_index(key);
    return const_iterator(this, index == npos ? bucket_count : index);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD iterator find_iter(const K& key) noexcept {
    return find_iterator(key);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  METL_NODISCARD const_iterator find_iter(const K& key) const noexcept {
    return find_iterator(key);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same<decay_t<K>, key_type>::value>>
  bool erase(const K& key) noexcept {
    const size_type index = find_existing_index(key);
    if (index == npos) {
      return false;
    }

    destroy_at(index, slot_state::tombstone);
    return true;
  }

  // ---- Modifiers ----

  /// @brief Insert @p key/@p value only if @p key is absent, without overflowing.
  /// @return @c true if inserted; @c false if the key already exists OR the map is at capacity.
  /// @note Unlike @c emplace, a full map or duplicate key is reported by the return value
  ///       rather than an assertion.
  template <typename K, typename V>
  bool try_emplace(K&& key, V&& value) {
    size_type index = npos;
    if (!locate_insert_index(key, &index)) {
      return false;
    }

    if (states_[index] == slot_state::occupied) {
      return false;
    }

    // Capacity is the user-requested element ceiling; bucket_count is the (larger) table size.
    // Refuse insertion past Capacity even when an empty/tombstone slot is still available.
    if (size_ >= Capacity) {
      return false;
    }

    construct_at(index, std::forward<K>(key), std::forward<V>(value));
    return true;
  }

  /// @brief Insert @p key/@p value, or return the existing element if @p key is already present.
  /// @return Reference to the inserted or pre-existing element (an existing value is NOT overwritten).
  /// @pre The map is not full when the key is absent; a violation asserts. Use @c try_emplace to
  ///      handle a full map without asserting.
  template <typename K, typename V>
  reference emplace(K&& key, V&& value) {
    // Find-existing first so a duplicate key never double-constructs over a
    // live element (which would also increment size_ twice).
    const size_type existing = find_existing_index(key);
    if (existing != npos) {
      return *slot_value(existing);
    }

    METL_ASSERT(size_ < Capacity);
    size_type index = npos;
    const bool available = locate_insert_index(key, &index);
    METL_ASSERT(available);
    METL_ASSERT(states_[index] != slot_state::occupied);
    construct_at(index, std::forward<K>(key), std::forward<V>(value));
    return *slot_value(index);
  }

  /// @brief Assign @p value to an existing @p key, or insert the pair if absent.
  /// @return @c true on assign or successful insert; @c false only if a new key cannot fit (full).
  template <typename K, typename V>
  bool insert_or_assign(K&& key, V&& value) {
    const size_type existing = find_existing_index(key);
    if (existing != npos) {
      slot_value(existing)->value = std::forward<V>(value);
      return true;
    }

    if (size_ >= Capacity) {
      return false;
    }

    size_type index = npos;
    if (!locate_insert_index(key, &index)) {
      return false;
    }

    construct_at(index, std::forward<K>(key), std::forward<V>(value));
    return true;
  }

  /// @brief Key-based subscript: return the mapped value for @p key, default-constructing and
  ///        inserting it if the key is absent (like @c std::unordered_map::operator[]).
  /// @return Reference to the mapped value.
  /// @pre The map is not full when the key is absent; a violation asserts.
  mapped_type& operator[](const key_type& key) {
    const size_type existing = find_existing_index(key);
    if (existing != npos) {
      return slot_value(existing)->value;
    }

    METL_ASSERT(size_ < Capacity);
    size_type index = npos;
    const bool available = locate_insert_index(key, &index);
    METL_ASSERT(available);
    METL_ASSERT(states_[index] != slot_state::occupied);
    construct_at(index, key, mapped_type{});
    return slot_value(index)->value;
  }

  /// @brief Key-based subscript (rvalue-key overload); inserts a default value if @p key is absent.
  /// @pre The map is not full when the key is absent; a violation asserts.
  mapped_type& operator[](key_type&& key) {
    const size_type existing = find_existing_index(key);
    if (existing != npos) {
      return slot_value(existing)->value;
    }

    METL_ASSERT(size_ < Capacity);
    size_type index = npos;
    const bool available = locate_insert_index(key, &index);
    METL_ASSERT(available);
    METL_ASSERT(states_[index] != slot_state::occupied);
    construct_at(index, static_cast<key_type&&>(key), mapped_type{});
    return slot_value(index)->value;
  }

  /// @brief Erase the element with the given key, if present (leaves a tombstone slot).
  /// @return @c true if an element was erased; @c false if the key was not found.
  bool erase(const key_type& key) noexcept {
    const size_type index = find_existing_index(key);
    if (index == npos) {
      return false;
    }

    destroy_at(index, slot_state::tombstone);
    return true;
  }

  /// @brief Remove all elements and reset every slot to empty (size becomes 0).
  void clear() noexcept {
    for (size_type i = 0; i < bucket_count; ++i) {
      if (states_[i] == slot_state::occupied) {
        destroy_at(i, slot_state::empty);
      } else {
        states_[i] = slot_state::empty;
      }
    }
  }

 private:
  void initialize_states() noexcept {
    for (size_type i = 0; i < bucket_count; ++i) {
      states_[i] = slot_state::empty;
    }
  }

  value_type* slot_value(size_type index) noexcept { return storage_[index].ptr(); }
  const value_type* slot_value(size_type index) const noexcept { return storage_[index].ptr(); }

  template <typename K>
  size_type bucket_index(const K& key) const noexcept {
    return static_cast<size_type>(hasher_(key)) & (bucket_count - 1);
  }

  template <typename K>
  size_type find_existing_index(const K& key) const noexcept {
    if (Capacity == 0) {
      return npos;
    }

    const size_type start = bucket_index(key);
    for (size_type probe = 0; probe < bucket_count; ++probe) {
      const size_type index = (start + probe) & (bucket_count - 1);
      if (states_[index] == slot_state::empty) {
        return npos;
      }
      if (states_[index] == slot_state::occupied && key_equal_(slot_value(index)->key, key)) {
        return index;
      }
    }
    return npos;
  }

  template <typename K>
  bool locate_insert_index(const K& key, size_type* index_out) const noexcept {
    if (Capacity == 0) {
      return false;
    }

    size_type first_tombstone = npos;
    const size_type start = bucket_index(key);
    for (size_type probe = 0; probe < bucket_count; ++probe) {
      const size_type index = (start + probe) & (bucket_count - 1);
      if (states_[index] == slot_state::empty) {
        *index_out = first_tombstone != npos ? first_tombstone : index;
        return true;
      }

      if (states_[index] == slot_state::tombstone) {
        if (first_tombstone == npos) {
          first_tombstone = index;
        }
        continue;
      }

      if (key_equal_(slot_value(index)->key, key)) {
        *index_out = index;
        return true;
      }
    }

    if (first_tombstone != npos) {
      *index_out = first_tombstone;
      return true;
    }

    return false;
  }

  template <typename K, typename V>
  void construct_at(size_type index, K&& key, V&& value) {
    // Hard guard against the full-table path: locate_insert_index returns
    // false (leaving index == npos) only when the table is full. The callers
    // assert on that precondition, but assert this unconditionally too so a
    // user-disabled METL_ASSERT can never turn a full-table insert into a wild
    // out-of-bounds construct_at(npos, ...).
    METL_ASSERT(index < bucket_count);
    ::new (storage_[index].addr()) value_type{std::forward<K>(key), std::forward<V>(value)};
    states_[index] = slot_state::occupied;
    ++size_;
  }

  void destroy_at(size_type index, slot_state next_state) noexcept {
    slot_value(index)->~value_type();
    states_[index] = next_state;
    --size_;
  }

  storage_for<value_type> storage_[bucket_count];
  slot_state states_[bucket_count];
  size_type size_;
  Hash hasher_;
  KeyEqual key_equal_;
};

}  // namespace metl
