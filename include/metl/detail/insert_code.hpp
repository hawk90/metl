#pragma once

#include "metl/attributes.hpp"
#include "metl/detail/abi.hpp"

#include <cstddef>

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {
// What an associative container's insert reports to the inline wrapper that
// builds its `pair<iterator, bool>`: the slot, and whether the insert filled it,
// in one word.
//
// One word, so the out-of-line insert returns it in a register. The pair
// itself is 8 bytes or more, and the Arm procedure call standard returns a
// composite over 4 bytes through memory: the caller reserves stack for it and
// loads `.second` back.
//
// A plain integer, not a class: when GCC inlines an insert whose branches
// each return a different class value, it builds the word and then tests bit
// 0, where for an integer it folds the test into each branch.
//
// Bit 0 holds "inserted"; the rest hold the slot. `full` has bit 0 clear, so
// it reads as not inserted.
struct insert_code {
  using type = std::size_t;

  static constexpr type full = ~static_cast<type>(1);

  static constexpr type inserted_at(type slot) noexcept { return (slot << 1u) | 1u; }
  static constexpr type found_at(type slot) noexcept { return slot << 1u; }

  METL_NODISCARD static constexpr bool inserted(type code) noexcept { return (code & 1u) != 0u; }
  METL_NODISCARD static constexpr bool is_full(type code) noexcept { return code == full; }
  // The slot; meaningless when is_full(code).
  METL_NODISCARD static constexpr type slot(type code) noexcept { return code >> 1u; }
};
}  // namespace detail
METL_DETAIL_ABI_END
}  // namespace metl
