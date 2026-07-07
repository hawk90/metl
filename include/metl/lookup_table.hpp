#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace metl {

template <typename Key, typename Value>
struct lookup_entry {
  Key key;
  Value value;
};

template <typename Key, typename Value, std::size_t Size>
class lookup_table {
 public:
  using key_type = Key;
  using mapped_type = Value;
  using value_type = lookup_entry<Key, Value>;
  using size_type = std::size_t;

  constexpr lookup_table() noexcept : entries_{} {}

  constexpr lookup_table(const std::array<value_type, Size>& entries) noexcept : entries_(entries) {}

  METL_NODISCARD constexpr size_type size() const noexcept { return Size; }
  METL_NODISCARD constexpr bool empty() const noexcept { return Size == 0; }

  METL_NODISCARD constexpr const value_type& operator[](size_type index) const noexcept {
    METL_ASSERT(index < Size);
    return entries_[index];
  }

  METL_NODISCARD constexpr const value_type& at(size_type index) const noexcept {
    METL_ASSERT(index < Size);
    return entries_[index];
  }

  METL_NODISCARD constexpr bool contains(const key_type& key) const noexcept { return find(key) != nullptr; }

  METL_NODISCARD constexpr const mapped_type* find(const key_type& key) const noexcept {
    for (size_type i = 0; i < Size; ++i) {
      if (entries_[i].key == key) {
        return &entries_[i].value;
      }
    }
    return nullptr;
  }

  METL_NODISCARD constexpr mapped_type value_or(const key_type& key, mapped_type fallback) const noexcept {
    const mapped_type* value = find(key);
    return value != nullptr ? *value : fallback;
  }

 private:
  std::array<value_type, Size> entries_;
};

template <typename Key, typename Value, std::size_t Size>
METL_NODISCARD constexpr lookup_table<Key, Value, Size> make_lookup_table(
    const std::array<lookup_entry<Key, Value>, Size>& entries) noexcept {
  return lookup_table<Key, Value, Size>(entries);
}

}  // namespace metl
