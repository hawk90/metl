#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace metl {

/// A single key/value pair stored in a lookup_table.
template <typename Key, typename Value>
struct lookup_entry {
  Key key;
  Value value;
};

/// Immutable key/value lookup table with a compile-time FIXED size.
///
/// Holds exactly `Size` entries in inline storage (a std::array); performs NO
/// heap allocation. Lookups are a linear scan, so it is intended for small,
/// often constexpr, tables. Entries are fixed at construction. Not thread-safe
/// for concurrent mutation, but const lookups are safe to share.
///
/// @tparam Key Key type (compared with operator==).
/// @tparam Value Mapped value type.
/// @tparam Size Number of entries (fixed at compile time).
template <typename Key, typename Value, std::size_t Size>
class lookup_table {
 public:
  using key_type = Key;
  using mapped_type = Value;
  using value_type = lookup_entry<Key, Value>;
  using size_type = std::size_t;

  /// Constructs a table with value-initialized entries.
  constexpr lookup_table() noexcept : entries_{} {}

  /// Constructs a table from an array of `Size` entries.
  constexpr lookup_table(const std::array<value_type, Size>& entries) noexcept : entries_(entries) {}

  /// Returns the fixed number of entries (`Size`).
  METL_NODISCARD constexpr size_type size() const noexcept { return Size; }
  /// Returns true if the table has no entries.
  METL_NODISCARD constexpr bool empty() const noexcept { return Size == 0; }

  /// Accesses the entry at `index`.
  /// @pre `index < size()`; out-of-range asserts and aborts (does not throw).
  METL_NODISCARD constexpr const value_type& operator[](size_type index) const noexcept {
    METL_ASSERT(index < Size);
    return entries_[index];
  }

  /// Accesses the entry at `index`. Unlike std::array::at, does NOT throw: an
  /// out-of-range index asserts and aborts by default.
  /// @pre `index < size()`.
  METL_NODISCARD constexpr const value_type& at(size_type index) const noexcept {
    METL_ASSERT(index < Size);
    return entries_[index];
  }

  /// Returns true if any entry has a key equal to `key`.
  METL_NODISCARD constexpr bool contains(const key_type& key) const noexcept { return find(key) != nullptr; }

  /// Finds the value mapped to `key` via a linear scan.
  /// @return Pointer to the mapped value, or nullptr if `key` is not present.
  METL_NODISCARD constexpr const mapped_type* find(const key_type& key) const noexcept {
    for (size_type i = 0; i < Size; ++i) {
      if (entries_[i].key == key) {
        return &entries_[i].value;
      }
    }
    return nullptr;
  }

  /// Returns the value mapped to `key`, or `fallback` if `key` is not present.
  METL_NODISCARD constexpr mapped_type value_or(const key_type& key, mapped_type fallback) const noexcept {
    const mapped_type* value = find(key);
    return value != nullptr ? *value : fallback;
  }

 private:
  std::array<value_type, Size> entries_;
};

/// Creates a lookup_table from an array of entries, deducing its template args.
template <typename Key, typename Value, std::size_t Size>
METL_NODISCARD constexpr lookup_table<Key, Value, Size> make_lookup_table(
    const std::array<lookup_entry<Key, Value>, Size>& entries) noexcept {
  return lookup_table<Key, Value, Size>(entries);
}

}  // namespace metl
