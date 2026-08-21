#pragma once

/// @file
/// @brief The integer-type constraint shared by `metl/format.hpp` and `metl/parse.hpp`.
///
/// Both headers convert between numbers and text, and both refuse the same two
/// categories for the same reason: `bool` and the character types are integers
/// to the language but almost never integers to the caller. `format_uint(out,
/// 'A')` writing `65`, or `parse_uint<char>` yielding a `char`, is a silent
/// surprise of exactly the kind docs/SCOPE.md forbids.
///
/// The trait lived in `format.hpp` first. It moved here when `parse.hpp` needed
/// the same rule, so that the two headers cannot drift into disagreeing about
/// what an integer is.

#include "metl/config.hpp"

#include <type_traits>

namespace metl {
namespace detail {

/// True for the integer types these conversions accept: every integral type
/// except `bool` and the character types.
template <typename T>
constexpr bool is_plain_integer_v =
    std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> &&
    !std::is_same_v<std::remove_cv_t<T>, char> && !std::is_same_v<std::remove_cv_t<T>, char16_t> &&
    !std::is_same_v<std::remove_cv_t<T>, char32_t> && !std::is_same_v<std::remove_cv_t<T>, wchar_t>;

}  // namespace detail
}  // namespace metl
