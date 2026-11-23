#pragma once

#include "metl/detail/abi.hpp"

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {
// Tag: build an unordered container's iterator at a slot known to be occupied
// (or at the end), skipping the scan for the next occupied slot.
struct at_slot_t {
  explicit at_slot_t() = default;
};
inline constexpr at_slot_t at_slot{};
}  // namespace detail
METL_DETAIL_ABI_END
}  // namespace metl
