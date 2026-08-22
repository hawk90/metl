#pragma once

/// @file
/// @brief Progress guarantees for `metl::static_unordered_set` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `find`, `contains`, `count` | wait-free, bounded by `bucket_count` probes |
///   | `insert`, `emplace` | wait-free, bounded by `bucket_count` probes |
///   | `erase` | wait-free, bounded by `bucket_count` probes **plus, occasionally, a full rebuild** |
///   | `clear`, iteration, copy, destructor | wait-free, bounded by `bucket_count` |
///
/// Open addressing with linear probing: the worst case is a probe run the length of
/// the table, and `bucket_count` is a power of two fixed at compile time.
///
/// That bound is the loop, not a consequence of good behaviour: the probe loop
/// counts to `bucket_count` and stops. It holds no matter how the table has been
/// used, and nothing below is needed to make it true. (An earlier version of this
/// paragraph said the reclaim below is what keeps the worst case from "drifting
/// upward". That was wrong, and worth correcting rather than quietly deleting: it
/// credited a real mechanism with preventing a hazard this implementation never
/// had, which makes the guarantee look contingent on an optimisation when it is
/// not.)
///
/// What the reclaim actually protects is the TYPICAL cost under churn. Erasure
/// leaves a tombstone, because clearing the slot would break the probe chain
/// running through it. A tombstone does not end a negative lookup -- only an empty
/// slot does -- so a table that only ever accumulated them would answer "not
/// present" by walking further and further, until every miss cost the full
/// `bucket_count`. Still bounded; steadily worse. So once tombstones pass one
/// eighth of the table, `rehash_in_place` rebuilds it without allocating, and
/// misses go back to stopping early.
///
/// The price is on `erase`, and it is why that row is split above. Most erases are
/// a probe run. The one that crosses the threshold also move-constructs every live
/// element -- up to `bucket_count` of them, plus the probing to re-place each. That
/// rebuild is bounded (it visits each slot once, and its inner carry loop
/// terminates because every iteration turns one more slot permanently occupied),
/// but it is a latency spike on an operation that is otherwise cheap, and a caller
/// with a deadline on `erase` needs to know it exists.
///
/// `tests/containers/unordered_reclaim_test.cpp` holds the reclaim to that: it
/// counts moves of a key type through an erase, which is zero unless a rebuild
/// fired. Without it the reclaim could be deleted and nothing would notice -- the
/// type stays correct, only slower, which no other test or fuzz harness can see.
///
/// Two bounds this header does not own: `Hash` and `KeyEqual` are called on the
/// probe path, so an unbounded hash or comparison makes every operation above
/// unbounded with it.
///
/// @par Memory footprint -- read this before picking a capacity
/// `bucket_count` is `bit_ceil(Capacity * 2)`, so the table always holds at
/// least **twice** the capacity you asked for, and every bucket carries a state
/// byte alongside its slot. `static_unordered_set<uint32_t, 256>` is **2584
/// bytes** against the 1024 bytes of keys it stores. There is also a cliff --
/// capacity 128 gets 256 buckets and capacity **129 gets 512** -- so **pick a
/// capacity at or just under a power of two**. `docs/CHOOSING.md` has the table
/// and the comparison with `flat_set`; `tests/core/ram_footprint_test.cpp`
/// asserts these numbers so the prose cannot drift away from the layout.
///
/// The elements live inline, so as a local this is a 2584-byte stack frame, and
/// METL cannot tell you whether that fit: the recoverable API answers "is the
/// container full", never "did the frame fit". Prefer static storage.
///
/// Single-threaded: this type does not synchronise.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/transparent.hpp"
#include "metl/hash.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

/// @brief Fixed-capacity set of unique keys using open addressing with linear probing.
///
/// Holds up to @c Capacity keys in place with NO heap allocation; capacity is fixed at compile
/// time. The bucket table is a power of two sized so probing uses a mask instead of modulo;
/// erased slots leave tombstones. Iteration order is unspecified. Not thread-safe.
///
/// @tparam Key Element/key type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
/// @tparam Hash Hash functor for keys (a transparent hasher plus transparent @c KeyEqual enables
///         heterogeneous lookup).
/// @tparam KeyEqual Equality comparator for keys.
template <typename Key,
          std::size_t Capacity,
          typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class static_unordered_set {
 public:
  using key_type = Key;
  using value_type = Key;
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
    using value_type = static_unordered_set::value_type;
    using pointer = value_type*;
    using reference = value_type&;
    using iterator_category = std::forward_iterator_tag;

    iterator() noexcept : set_(nullptr), index_(0) {}

    reference operator*() const noexcept { return *set_->slot_value(index_); }
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
      return lhs.set_ == rhs.set_ && lhs.index_ == rhs.index_;
    }

    friend bool operator!=(const iterator& lhs, const iterator& rhs) noexcept { return !(lhs == rhs); }

   private:
    friend class static_unordered_set;

    iterator(static_unordered_set* set, size_type index) noexcept : set_(set), index_(index) {
      skip_to_occupied();
    }

    void skip_to_occupied() noexcept {
      if (set_ == nullptr) {
        return;
      }

      while (index_ < bucket_count && set_->states_[index_] != slot_state::occupied) {
        ++index_;
      }
    }

    static_unordered_set* set_;
    size_type index_;
  };

  /// @brief Const forward iterator over occupied slots (skips empty and tombstone slots).
  class const_iterator {
   public:
    using difference_type = std::ptrdiff_t;
    using value_type = static_unordered_set::value_type;
    using pointer = const value_type*;
    using reference = const value_type&;
    using iterator_category = std::forward_iterator_tag;

    const_iterator() noexcept : set_(nullptr), index_(0) {}
    const_iterator(iterator other) noexcept : set_(other.set_), index_(other.index_) {}

    reference operator*() const noexcept { return *set_->slot_value(index_); }
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
      return lhs.set_ == rhs.set_ && lhs.index_ == rhs.index_;
    }

    friend bool operator!=(const const_iterator& lhs, const const_iterator& rhs) noexcept {
      return !(lhs == rhs);
    }

   private:
    friend class static_unordered_set;

    const_iterator(const static_unordered_set* set, size_type index) noexcept : set_(set), index_(index) {
      skip_to_occupied();
    }

    void skip_to_occupied() noexcept {
      if (set_ == nullptr) {
        return;
      }

      while (index_ < bucket_count && set_->states_[index_] != slot_state::occupied) {
        ++index_;
      }
    }

    const static_unordered_set* set_;
    size_type index_;
  };

  /// @brief Construct an empty set with all slots marked empty.
  static_unordered_set() noexcept : size_(0), hasher_(), key_equal_() { initialize_states(); }

  /// @brief Copy-construct, re-inserting every element from @p other.
  static_unordered_set(const static_unordered_set& other)
      : size_(0), hasher_(other.hasher_), key_equal_(other.key_equal_) {
    initialize_states();
    for (const auto& item : other) {
      emplace(item);
    }
  }

  /// @brief Move-construct, moving elements out of @p other and leaving it empty.
  static_unordered_set(static_unordered_set&& other) noexcept(
      std::is_nothrow_move_constructible_v<value_type>)
      : size_(0),
        hasher_(static_cast<Hash&&>(other.hasher_)),
        key_equal_(static_cast<KeyEqual&&>(other.key_equal_)) {
    initialize_states();
    for (auto& item : other) {
      emplace(static_cast<Key&&>(item));
    }
    other.clear();
  }

  /// @brief Destroy all contained elements.
  ~static_unordered_set() { clear(); }

  /// @brief Copy-assign from @p other (self-assignment safe).
  static_unordered_set& operator=(const static_unordered_set& other) {
    if (this == &other) {
      return *this;
    }

    clear();
    hasher_ = other.hasher_;
    key_equal_ = other.key_equal_;
    for (const auto& item : other) {
      emplace(item);
    }
    return *this;
  }

  /// @brief Move-assign from @p other, leaving it empty (self-assignment safe).
  static_unordered_set& operator=(static_unordered_set&& other) noexcept(
      std::is_nothrow_move_constructible_v<value_type> && std::is_nothrow_move_assignable_v<Hash> &&
      std::is_nothrow_move_assignable_v<KeyEqual>) {
    if (this == &other) {
      return *this;
    }

    clear();
    hasher_ = static_cast<Hash&&>(other.hasher_);
    key_equal_ = static_cast<KeyEqual&&>(other.key_equal_);
    for (auto& item : other) {
      emplace(static_cast<Key&&>(item));
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

  /// @brief True if the set holds no elements.
  METL_NODISCARD bool empty() const noexcept { return size_ == 0; }
  /// @brief True if the set has reached its fixed capacity.
  METL_NODISCARD bool full() const noexcept { return size_ == Capacity; }
  /// @brief Current number of elements.
  METL_NODISCARD size_type size() const noexcept { return size_; }
  /// @brief Fixed maximum number of elements (the compile-time @c Capacity).
  METL_NODISCARD size_type capacity() const noexcept { return Capacity; }

  /// @brief True if the given key is present.
  METL_NODISCARD bool contains(const key_type& key) const noexcept { return find(key) != nullptr; }

  /// @brief Key lookup: pointer to the stored element equal to @p key, or @c nullptr if absent.
  /// @return Pointer to the element, or @c nullptr when the key is not found.
  METL_NODISCARD value_type* find(const key_type& key) noexcept {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : slot_value(index);
  }

  METL_NODISCARD const value_type* find(const key_type& key) const noexcept {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : slot_value(index);
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

  // ---- Heterogeneous lookup overloads ----
  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD bool contains(const K& key) const noexcept {
    return find_existing_index(key) != npos;
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD value_type* find(const K& key) noexcept {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : slot_value(index);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const value_type* find(const K& key) const noexcept {
    const size_type index = find_existing_index(key);
    return index == npos ? nullptr : slot_value(index);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD iterator find_iterator(const K& key) noexcept {
    const size_type index = find_existing_index(key);
    return iterator(this, index == npos ? bucket_count : index);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const_iterator find_iterator(const K& key) const noexcept {
    const size_type index = find_existing_index(key);
    return const_iterator(this, index == npos ? bucket_count : index);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD iterator find_iter(const K& key) noexcept {
    return find_iterator(key);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  METL_NODISCARD const_iterator find_iter(const K& key) const noexcept {
    return find_iterator(key);
  }

  template <typename K,
            typename = enable_if_t<detail::is_transparent_v<Hash, KeyEqual> &&
                                   !std::is_same_v<decay_t<K>, key_type>>>
  bool erase(const K& key) noexcept {
    const size_type index = find_existing_index(key);
    if (index == npos) {
      return false;
    }

    destroy_at(index, slot_state::tombstone);
    reclaim_if_needed();
    return true;
  }

  // ---- Modifiers ----

  /// @brief Insert @p key only if it is absent, without overflowing.
  /// @return @c true if inserted; @c false if the key already exists OR the set is at capacity.
  /// @note Unlike @c emplace, a full set or duplicate key is reported by the return value
  ///       rather than an assertion.
  template <typename K>
  METL_NODISCARD bool try_emplace(K&& key) {
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

    construct_at(index, std::forward<K>(key));
    return true;
  }

  /// @brief Insert @p key and return a reference to the stored element.
  /// @return Reference to the inserted (or, for a duplicate key, the matching) element.
  /// @pre The set is not full when the key is absent; a violation asserts. Use @c try_emplace to
  ///      handle a full set without asserting.
  template <typename K>
  reference emplace(K&& key) {
    size_type index = npos;
    const bool available = locate_insert_index(key, &index);
    METL_ASSERT(available);
    METL_ASSERT(states_[index] != slot_state::occupied);
    METL_ASSERT(size_ < Capacity);
    construct_at(index, std::forward<K>(key));
    return *slot_value(index);
  }

  /// @brief Erase the element equal to the given key, if present (leaves a tombstone slot).
  /// @return @c true if an element was erased; @c false if the key was not found.
  bool erase(const key_type& key) noexcept {
    const size_type index = find_existing_index(key);
    if (index == npos) {
      return false;
    }

    destroy_at(index, slot_state::tombstone);
    reclaim_if_needed();
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
  void initialize_states() noexcept {
    for (size_type i = 0; i < bucket_count; ++i) {
      states_[i] = slot_state::empty;
    }
    tombstones_ = 0;
  }

  value_type* slot_value(size_type index) noexcept { return storage_[index].ptr(); }
  const value_type* slot_value(size_type index) const noexcept { return storage_[index].ptr(); }

  template <typename K>
  size_type bucket_index(const K& key) const noexcept {
    // Finalize/avalanche the hash so high-entropy bits reach the low bits that the mask keeps.
    // insert and lookup both route through here, so they always agree on the bucket.
    return static_cast<size_type>(detail::hash_mix(hasher_(key))) & (bucket_count - 1);
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
      if (states_[index] == slot_state::occupied && key_equal_(*slot_value(index), key)) {
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

      if (key_equal_(*slot_value(index), key)) {
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

  template <typename K>
  void construct_at(size_type index, K&& key) {
    // Always-on hard guard mirroring static_unordered_map: locate_insert_index
    // leaves index == npos only on a full table, and this must never escalate
    // into a wild out-of-bounds construct_at even at METL_HARDENING_NONE or with
    // a user-disabled METL_ASSERT.
    METL_HARDEN(index < bucket_count);
    if (states_[index] == slot_state::tombstone) {
      // Reusing a tombstone slot reclaims it: keep the tombstone count accurate
      // so the reclamation threshold reflects only live tombstones.
      --tombstones_;
    }
    ::new (storage_[index].addr()) value_type(std::forward<K>(key));
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

  /// @brief Rebuild the table in place, clearing every tombstone, so lookups stay bounded.
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
  void rehash_in_place() noexcept {
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
        size_type target = bucket_index(carry.ref());
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

  /// @brief Trigger an in-place rebuild once tombstones cross ~1/8 of the table.
  /// Bounds the tombstone density so negative lookups always terminate at an empty slot.
  void reclaim_if_needed() noexcept {
    if (tombstones_ > bucket_count / 8) {
      rehash_in_place();
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
