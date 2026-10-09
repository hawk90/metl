#pragma once

/// @file
/// @brief Progress guarantees for `metl::static_unordered_map` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `find`, `contains`, `find_iterator` | wait-free, bounded by `bucket_count` probes |
///   | new key, incl. `operator[]` | wait-free, `bucket_count` probes **plus, at times, a rebuild** |
///   | `erase` | wait-free, bounded by `bucket_count` probes; moves nothing |
///   | `clear`, iteration, destructor | wait-free, bounded by `bucket_count` |
///   | copy, move (construct and assign) | wait-free, one insert of <= `bucket_count` probes per element |
///
/// Open addressing with linear probing: the worst case is a probe run the length of
/// the table, and `bucket_count` is a power of two fixed at compile time. Copy and
/// move scan the source's `bucket_count` slots and re-insert each element into a
/// table with no tombstones, so they never trigger a rebuild.
///
/// That bound is the loop, not a consequence of good behaviour: the probe loop
/// counts to `bucket_count` and stops. It holds no matter how the table has been
/// used, and nothing below is needed to make it true. In particular the reclaim
/// below does not keep the worst case from growing -- it never could grow -- so
/// the guarantee is not contingent on that optimisation.
///
/// What the reclaim actually protects is the TYPICAL cost under churn. Erasure
/// leaves a tombstone, because clearing the slot would break the probe chain
/// running through it. A tombstone does not end a negative lookup -- only an empty
/// slot does -- so a table that only ever accumulated them would answer "not
/// present" by walking further and further, until every miss cost the full
/// `bucket_count`. Still bounded; steadily worse. So once tombstones pass one
/// eighth of the table, the next insertion of a new key runs `rehash_in_place`
/// first, which rebuilds the table without allocating, and misses go back to
/// stopping early.
///
/// The price is on inserting a new key, and it is why that row is split above.
/// Most inserts are a probe run. The one that finds the threshold crossed also
/// move-constructs every live element -- up to `Capacity` of them, plus the
/// probing to re-place each. That rebuild is bounded (it visits each slot once,
/// and its inner carry loop terminates because every iteration turns one more
/// slot permanently occupied), but it is a latency spike on an operation that is
/// otherwise cheap, and a caller with a deadline on insertion needs to know it
/// exists.
///
/// @par Element moves during the rebuild
/// The rebuild relocates every element in place, which cannot be undone half
/// way. It therefore runs only when the element's move cannot throw; for any
/// other type the tombstones are simply never reclaimed -- lookups stay bounded
/// by `bucket_count` probes, only misses get slower under churn. A hasher that
/// throws during the rebuild empties the table and propagates.
///
/// @par Iterator invalidation
/// The open-addressing rule (`absl::flat_hash_map`, `boost::unordered_flat_map`):
/// **`erase` invalidates only iterators, pointers and references to the erased
/// element**, so the erase-while-iterating loop (`auto k = it->key; ++it;
/// m.erase(k);`) visits every element. **Inserting a new key may invalidate all
/// iterators, pointers and references**, because that is where the rebuild runs
/// -- unlike node-based `std::unordered_map`, a pointer from `find` does not
/// survive it.
/// Assigning to an existing key and lookups invalidate nothing. The rebuild never
/// runs inside `erase`, because there it would skip elements during iteration.
///
/// `tests/containers/unordered_reclaim_test.cpp` holds the reclaim to that: it
/// counts moves of a key type through an insert, which is one unless a rebuild
/// fired -- and through an erase, which must always be zero. Without it the
/// reclaim could be deleted and nothing would notice -- the type stays correct,
/// only slower, which no other test or fuzz harness can see.
///
/// @par Memory footprint -- read this before picking a capacity
/// `bucket_count` is `bit_ceil(Capacity * 2)`, so the table always holds at
/// least **twice** the capacity you asked for, and every bucket carries a state
/// byte alongside its slot. `static_unordered_map<uint32_t, uint64_t, 256>` is
/// **8728 bytes** against the 3072 bytes of key-and-value it stores: 2.84x. The
/// multiplier never drops below about 2.8x, and there is a cliff -- `bit_ceil`
/// means capacity 128 gets 256 buckets and capacity **129 gets 512**, so one
/// more element costs 4352 more bytes. **Pick a capacity at or just under a
/// power of two.** `docs/CHOOSING.md` has the table and the comparison with
/// `flat_map` (a flat 1.34x, no cliff); `tests/core/ram_footprint_test.cpp`
/// asserts these numbers so the prose cannot drift away from the layout.
///
/// The elements live inline, so this is a real 8728-byte object: as a local it
/// is an 8728-byte stack frame, and METL cannot tell you whether that fit. The
/// recoverable API answers "is the container full", never "did the frame fit".
/// Prefer static storage, or a member of something already in static storage.
///
/// Two bounds this header does not own: `Hash` and `KeyEqual` are called on the
/// probe path, so an unbounded hash or comparison makes every operation above
/// unbounded with it.
///
/// Single-threaded: this type does not synchronise.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/addressof.hpp"
#include "metl/detail/nothrow_call.hpp"
#include "metl/detail/transparent.hpp"
#include "metl/hash.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

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
  static_assert(std::is_object_v<Key> && !std::is_array_v<Key>, METL_DETAIL_OBJECT_TYPE_MESSAGE);
  static_assert(std::is_object_v<T> && !std::is_array_v<T>, METL_DETAIL_OBJECT_TYPE_MESSAGE);

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
    pointer operator->() const noexcept { return detail::addressof(**this); }

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
    pointer operator->() const noexcept { return detail::addressof(**this); }

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
  // Element-inserting constructors delegate to an empty constructor first.
  // Once it returns the object is fully constructed, so if copying or moving
  // an element throws part-way, the destructor runs and destroys exactly the
  // elements already inserted; nothing leaks.
  static_unordered_map(const static_unordered_map& other)
      : static_unordered_map(empty_with{}, other.hasher_, other.key_equal_) {
    for (const auto& item : other) {
      emplace(item.key, item.value);
    }
  }

  /// @brief Move-construct, moving elements out of @p other and leaving it empty.
  //
  // The elements are re-inserted, which runs the hasher and key comparison, so
  // the move is noexcept only when those are too (a throw from one used to end
  // in std::terminate).
  static_unordered_map(static_unordered_map&& other) noexcept(
      std::is_nothrow_move_constructible_v<value_type> && lookup_cannot_throw<key_type>)
      : static_unordered_map(
            empty_with{}, static_cast<Hash&&>(other.hasher_), static_cast<KeyEqual&&>(other.key_equal_)) {
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
      std::is_nothrow_move_constructible_v<value_type> && std::is_nothrow_move_assignable_v<Hash> &&
      std::is_nothrow_move_assignable_v<KeyEqual> && lookup_cannot_throw<key_type>) {
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
  METL_NODISCARD bool contains(const key_type& key) const noexcept(lookup_cannot_throw<key_type>) {
    return find(key) != nullptr;
  }

  /// @brief Key lookup: pointer to the mapped value for @p key, or @c nullptr if absent.
  /// @return Pointer to the mapped value, or @c nullptr when the key is not found.
  METL_NODISCARD mapped_type* find(const key_type& key) noexcept(lookup_cannot_throw<key_type>) {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : detail::addressof(slot_value(index)->value);
  }

  METL_NODISCARD const mapped_type* find(const key_type& key) const noexcept(lookup_cannot_throw<key_type>) {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : detail::addressof(slot_value(index)->value);
  }

  /// @brief Key lookup returning an iterator, or @c end() if the key is absent.
  METL_NODISCARD iterator find_iterator(const key_type& key) noexcept(lookup_cannot_throw<key_type>) {
    const size_type index = find_existing_index(key);
    return iterator(this, index == npos ? bucket_count : index);
  }

  METL_NODISCARD const_iterator find_iterator(const key_type& key) const
      noexcept(lookup_cannot_throw<key_type>) {
    const size_type index = find_existing_index(key);
    return const_iterator(this, index == npos ? bucket_count : index);
  }

  /// @brief STL-compatible iterator-returning find (alias for @c find_iterator).
  METL_NODISCARD iterator find_iter(const key_type& key) noexcept(lookup_cannot_throw<key_type>) {
    return find_iterator(key);
  }
  METL_NODISCARD const_iterator find_iter(const key_type& key) const noexcept(lookup_cannot_throw<key_type>) {
    return find_iterator(key);
  }

  // ---- Heterogeneous lookup overloads (enabled when both Hash and KeyEqual are transparent) ----
  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD bool contains(const K& key) const noexcept(lookup_cannot_throw<K>) {
    return find_existing_index(key) != npos;
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD mapped_type* find(const K& key) noexcept(lookup_cannot_throw<K>) {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : detail::addressof(slot_value(index)->value);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const mapped_type* find(const K& key) const noexcept(lookup_cannot_throw<K>) {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : detail::addressof(slot_value(index)->value);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD iterator find_iterator(const K& key) noexcept(lookup_cannot_throw<K>) {
    const size_type index = find_existing_index(key);
    return iterator(this, index == npos ? bucket_count : index);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const_iterator find_iterator(const K& key) const noexcept(lookup_cannot_throw<K>) {
    const size_type index = find_existing_index(key);
    return const_iterator(this, index == npos ? bucket_count : index);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD iterator find_iter(const K& key) noexcept(lookup_cannot_throw<K>) {
    return find_iterator(key);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const_iterator find_iter(const K& key) const noexcept(lookup_cannot_throw<K>) {
    return find_iterator(key);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  bool erase(const K& key) noexcept(lookup_cannot_throw<K>) {
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
  METL_NODISCARD bool try_emplace(K&& key, V&& value) {
    if constexpr (!std::is_same_v<std::decay_t<K>, key_type>) {
      // Convert first, so the position, the duplicate check and the stored
      // key all come from the same value; a narrowing conversion would
      // otherwise store a key that does not belong where the argument put it.
      // Direct-initialised, not cast, so a consumer still gets the conversion
      // warning a narrowing key deserves.
      key_type converted{std::forward<K>(key)};
      return try_emplace(static_cast<key_type&&>(converted), std::forward<V>(value));
    } else {
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

      (void)construct_at(index, std::forward<K>(key), std::forward<V>(value));
      return true;
    }
  }

  /// @brief Insert @p key/@p value, or return the existing element if @p key is already present.
  /// @return Reference to the inserted or pre-existing element (an existing value is NOT overwritten).
  /// @pre The map is not full when the key is absent; a violation asserts. Use @c try_emplace to
  ///      handle a full map without asserting.
  template <typename K, typename V>
  reference emplace(K&& key, V&& value) {
    if constexpr (!std::is_same_v<std::decay_t<K>, key_type>) {
      // Convert first, so the position, the duplicate check and the stored
      // key all come from the same value; a narrowing conversion would
      // otherwise store a key that does not belong where the argument put it.
      // Direct-initialised, not cast, so a consumer still gets the conversion
      // warning a narrowing key deserves.
      key_type converted{std::forward<K>(key)};
      return emplace(static_cast<key_type&&>(converted), std::forward<V>(value));
    } else {
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
      index = construct_at(index, std::forward<K>(key), std::forward<V>(value));
      return *slot_value(index);
    }
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
      // Direct-initialised, not cast, so a consumer still gets the conversion
      // warning a narrowing key deserves.
      key_type converted{std::forward<K>(key)};
      return try_insert_or_assign(static_cast<key_type&&>(converted), std::forward<V>(value));
    } else {
      return insert_or_assign_impl(std::forward<K>(key), std::forward<V>(value)) != npos;
    }
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
      // Direct-initialised, not cast, so a consumer still gets the conversion
      // warning a narrowing key deserves.
      key_type converted{std::forward<K>(key)};
      return insert_or_assign(static_cast<key_type&&>(converted), std::forward<V>(value));
    } else {
      const size_type index = insert_or_assign_impl(std::forward<K>(key), std::forward<V>(value));
      METL_ASSERT(index != npos);
      // A refused insert returns npos, and METL_ASSERT is stripped at low
      // hardening levels; without this, slot_value(npos) would be a wild read.
      METL_HARDEN(index < bucket_count);
      return *slot_value(index);
    }
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
    index = construct_at(index, key, mapped_type{});
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
    index = construct_at(index, static_cast<key_type&&>(key), mapped_type{});
    return slot_value(index)->value;
  }

  /// @brief Erase the element with the given key, if present (leaves a tombstone slot).
  /// @return @c true if an element was erased; @c false if the key was not found.
  bool erase(const key_type& key) noexcept(lookup_cannot_throw<key_type>) {
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
    tombstones_ = 0;
  }

 private:
  // Lookups call the hasher and the key equality; they are noexcept exactly
  // when neither can throw (see detail/nothrow_call.hpp).
  template <typename K>
  static constexpr bool lookup_cannot_throw =
      detail::nothrow_unary_call_v<Hash, const K&> &&
      detail::nothrow_binary_call_v<KeyEqual, const key_type&, const K&>;
  static constexpr bool hash_cannot_throw = detail::nothrow_unary_call_v<Hash, const key_type&>;

  // The one call in rehash_in_place that can throw: element moves cannot (the
  // rebuild only runs for nothrow-movable elements), the hasher may. When it
  // does, `carry` holds a live element and every slot marked occupied (placed)
  // or tombstone (not yet placed) holds one too; destroy exactly those and empty
  // the table, as a throwing insert elsewhere in the library does, and let the
  // exception propagate.
  size_type bucket_during_rebuild(storage_for<value_type>& carry) noexcept(hash_cannot_throw) {
    if constexpr (hash_cannot_throw) {
      return bucket_index(carry.ptr()->key);
    } else {
#if METL_NO_EXCEPTIONS
      return bucket_index(carry.ptr()->key);
#else
      try {
        return bucket_index(carry.ptr()->key);
      } catch (...) {
        carry.ptr()->~value_type();
        for (size_type i = 0; i < bucket_count; ++i) {
          if (states_[i] != slot_state::empty) {
            slot_value(i)->~value_type();
            states_[i] = slot_state::empty;
          }
        }
        size_ = 0;
        tombstones_ = 0;
        throw;
      }
#endif
    }
  }
  // The rebuild moves every element in place, and a move that threw half way
  // would leave the table unrecoverable, so it runs only for nothrow-movable
  // elements. rehash_in_place is noexcept(hash_cannot_throw): a throwing hasher
  // empties the table and propagates (bucket_during_rebuild). The hasher is not part
  // of the condition -- requiring `noexcept` on it would silently switch the
  // reclaim off for every ordinary hasher that merely omits the keyword.
  static constexpr bool rebuild_cannot_throw = std::is_nothrow_move_constructible_v<value_type>;
  // Empty table with the given hasher and key-equal; the copy and move
  // constructors delegate here.
  struct empty_with {};
  template <typename H, typename E>
  static_unordered_map(empty_with, H&& hasher, E&& key_equal) noexcept(
      std::is_nothrow_constructible_v<Hash, H&&> && std::is_nothrow_constructible_v<KeyEqual, E&&>)
      : size_(0), hasher_(std::forward<H>(hasher)), key_equal_(std::forward<E>(key_equal)) {
    initialize_states();
  }

  void initialize_states() noexcept {
    for (size_type i = 0; i < bucket_count; ++i) {
      states_[i] = slot_state::empty;
    }
    tombstones_ = 0;
  }

  value_type* slot_value(size_type index) noexcept { return storage_[index].ptr(); }
  const value_type* slot_value(size_type index) const noexcept { return storage_[index].ptr(); }

  template <typename K>
  size_type bucket_index(const K& key) const noexcept(lookup_cannot_throw<K>) {
    // Finalize/avalanche the hash so high-entropy bits reach the low bits that the mask keeps.
    // insert and lookup both route through here, so they always agree on the bucket.
    return static_cast<size_type>(detail::hash_mix(hasher_(key))) & (bucket_count - 1);
  }

  template <typename K>
  size_type find_existing_index(const K& key) const noexcept(lookup_cannot_throw<K>) {
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
  bool locate_insert_index(const K& key, size_type* index_out) const noexcept(lookup_cannot_throw<K>) {
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

  /// Shared body of try_insert_or_assign / insert_or_assign.
  /// @return Index of the assigned-to or newly inserted slot, or `npos` if a new
  ///         key does not fit. Returning the index rather than a bool is what lets
  ///         the asserting form hand back a reference without a second lookup.
  template <typename K, typename V>
  size_type insert_or_assign_impl(K&& key, V&& value) {
    const size_type existing = find_existing_index(key);
    if (existing != npos) {
      slot_value(existing)->value = std::forward<V>(value);
      return existing;
    }

    if (size_ >= Capacity) {
      return npos;
    }

    size_type index = npos;
    if (!locate_insert_index(key, &index)) {
      return npos;
    }

    return construct_at(index, std::forward<K>(key), std::forward<V>(value));
  }

  /// Places a new element at `index` (from locate_insert_index) and returns the
  /// slot it ended up in. That is `index` unless the tombstone reclaim runs
  /// first: it runs here, on insertion of a new key, and never on erase, so
  /// erasing never moves another element and iteration survives it.
  /// The rebuild moves elements, and `value` may refer to
  /// one of them, so on that path the new element is built before the rebuild.
  template <typename K, typename V>
  METL_NODISCARD size_type construct_at(size_type index, K&& key, V&& value) {
    METL_HARDEN(index < bucket_count);
    // Tombstones past ~1/8 of the table: rebuild before placing the new key, so
    // negative lookups keep stopping early at an empty slot.
    if (rebuild_cannot_throw && tombstones_ > bucket_count / 8) {
      value_type entry{std::forward<K>(key), std::forward<V>(value)};
      rehash_in_place();
      // After a rebuild there are no tombstones and the load factor is at most
      // 1/2, so an empty slot always exists. `index` is reset so that a failed
      // locate leaves npos and trips the same guard as the ordinary path -- one
      // shared expression string, not a new one in .rodata.
      index = npos;
      (void)locate_insert_index(entry.key, &index);
      METL_HARDEN(index < bucket_count);
      ::new (storage_[index].addr()) value_type(static_cast<value_type&&>(entry));
      states_[index] = slot_state::occupied;
      ++size_;
      return index;
    }
    place_at(index, std::forward<K>(key), std::forward<V>(value));
    return index;
  }

  template <typename K, typename V>
  void place_at(size_type index, K&& key, V&& value) {
    // Hard guard against the full-table path: locate_insert_index returns
    // false (leaving index == npos) only when the table is full. The callers
    // assert on that precondition, but guard this with the always-on METL_HARDEN
    // (never stripped, even at METL_HARDENING_NONE) so neither a low hardening
    // level nor a user-disabled METL_ASSERT can turn a full-table insert into a
    // wild out-of-bounds construct_at(npos, ...).
    METL_HARDEN(index < bucket_count);
    ::new (storage_[index].addr()) value_type{std::forward<K>(key), std::forward<V>(value)};
    // Reusing a tombstone slot reclaims it: keep the tombstone count accurate
    // so the reclamation threshold reflects only live tombstones. Counted after
    // the construction succeeds -- before it, each throwing insert onto the same
    // tombstone decremented again, and the count wrapped.
    if (states_[index] == slot_state::tombstone) {
      --tombstones_;
    }
    states_[index] = slot_state::occupied;
    ++size_;
  }

  void destroy_at(size_type index, slot_state next_state) noexcept {
    slot_value(index)->~value_type();
    states_[index] = next_state;
    --size_;
    if (next_state == slot_state::tombstone) {
      ++tombstones_;
    }
  }

  /// @brief Rebuild the table in place, clearing every tombstone, so misses stop early again.
  ///
  /// Open addressing turns each erase into a tombstone that negative probes must still scan.
  /// Under sustained insert/erase churn these accumulate; once no @c empty slot remains, a
  /// missing-key lookup degrades to a full-table O(bucket_count) scan. This compacts every live
  /// element back to a gap-free probe run and marks all other slots empty, in place and heap-free
  /// (only two @c value_type temporaries on the stack), without changing @c size_.
  ///
  /// Live keys are re-placed through the SAME @c bucket_index() (identical avalanche mix), so the
  /// distribution is unchanged. Because the load factor is <= 50% (bucket_count >= 2*Capacity) an
  /// empty/unplaced slot always terminates each probe walk, so the inner loops cannot spin.
  void rehash_in_place() noexcept(hash_cannot_throw) {
    if (Capacity == 0) {
      return;
    }

    // Phase 1: turn real tombstones into empty and mark every live element as
    // "unplaced" by reusing the tombstone state as a transient marker. During
    // the rebuild no genuine tombstones exist, so this reuse is unambiguous.
    for (size_type i = 0; i < bucket_count; ++i) {
      states_[i] = (states_[i] == slot_state::occupied) ? slot_state::tombstone : slot_state::empty;
    }
    tombstones_ = 0;

    // Phase 2: place each unplaced element at the first slot in its probe run
    // that is not already occupied, cascading through any element it displaces.
    storage_for<value_type> carry;
    for (size_type i = 0; i < bucket_count; ++i) {
      if (states_[i] != slot_state::tombstone) {
        continue;
      }

      ::new (carry.addr()) value_type(static_cast<value_type&&>(*slot_value(i)));
      slot_value(i)->~value_type();
      states_[i] = slot_state::empty;

      for (;;) {
        size_type target = bucket_during_rebuild(carry);
        while (states_[target] == slot_state::occupied) {
          target = (target + 1) & (bucket_count - 1);
        }

        if (states_[target] == slot_state::empty) {
          ::new (storage_[target].addr()) value_type(static_cast<value_type&&>(carry.ref()));
          carry.ptr()->~value_type();
          states_[target] = slot_state::occupied;
          break;
        }

        // states_[target] is an unplaced element: settle carry here and continue
        // placing the element it displaced.
        storage_for<value_type> displaced;
        ::new (displaced.addr()) value_type(static_cast<value_type&&>(*slot_value(target)));
        slot_value(target)->~value_type();
        ::new (storage_[target].addr()) value_type(static_cast<value_type&&>(carry.ref()));
        carry.ptr()->~value_type();
        states_[target] = slot_state::occupied;
        ::new (carry.addr()) value_type(static_cast<value_type&&>(displaced.ref()));
        displaced.ptr()->~value_type();
      }
    }
  }

  storage_for<value_type> storage_[bucket_count];
  slot_state states_[bucket_count];
  size_type size_;
  size_type tombstones_;
  Hash hasher_;
  KeyEqual key_equal_;
};

}  // namespace metl
