#pragma once

/// @file
/// @brief `std::array<T, N>` for `N > 0`, and an empty type for `N == 0`, for the
///        tables of `fsm` and `lookup_table` (#696).
///
/// How big a zero-length `std::array` is depends on the standard library.
/// libstdc++ makes it an empty struct of 1 B. libc++ reserves room for one `T`, so
/// that `data()` can point at something. `fsm`'s hook tables default to zero
/// entries, so with libc++ every `fsm` without hooks carried two unused hooks:
/// `fsm<st, ev, 1>` was 80 B on a 64-bit host against 40 B with libstdc++.
///
/// The empty form is 1 B on every standard library, as libstdc++'s already is.
/// `N > 0` is `std::array` itself, so those layouts and their code are unchanged.
/// The classes that hold one keep `std::array` in their constructors' signatures
/// and never index the empty form: their element loops are under
/// `if constexpr (N > 0)`.

#include "metl/detail/abi.hpp"

#include <array>
#include <cstddef>
#include <type_traits>

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {

/// @brief A table of no `T`s. It holds nothing, and is built from the
///        zero-length `std::array` that the owning class's constructor takes.
template <typename T>
struct empty_array {
  constexpr empty_array() noexcept = default;
  constexpr explicit empty_array(const std::array<T, 0>& /*entries*/) noexcept {}
};

template <typename T, std::size_t N>
using array_or_empty_t = std::conditional_t<N == 0, empty_array<T>, std::array<T, N>>;

}  // namespace detail
METL_DETAIL_ABI_END
}  // namespace metl
