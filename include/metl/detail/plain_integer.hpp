#pragma once

/// @file
/// @brief The integer-type constraint shared by `metl/format.hpp` and `metl/parse.hpp`.
///
/// Both headers convert between numbers and text, and both refuse the same
/// types for the same reason: `bool` and the plain character types (`char`,
/// `wchar_t`, `char16_t`, `char32_t`) are integers to the language but almost
/// never integers to the caller. `format_uint(out, 'A')` writing `65`, or
/// `parse_uint<char>` yielding a `char`, is a silent surprise of exactly the
/// kind docs/SCOPE.md forbids. `signed char` and `unsigned char` are accepted:
/// they are how `std::int8_t` and `std::uint8_t` are spelled.
///
/// The trait lives in one header shared by both, so that they cannot drift into
/// disagreeing about what an integer is.

#include "metl/config.hpp"

#include <type_traits>

namespace metl {
namespace detail {

/// True for the integer types these conversions accept: every integral type
/// (cv-qualified or not) except `bool`, `char`, `wchar_t`, `char16_t` and
/// `char32_t`. `signed char` and `unsigned char` pass. (C++20's `char8_t` is
/// not excluded.)
template <typename T>
constexpr bool is_plain_integer_v =
    std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> &&
    !std::is_same_v<std::remove_cv_t<T>, char> && !std::is_same_v<std::remove_cv_t<T>, char16_t> &&
    !std::is_same_v<std::remove_cv_t<T>, char32_t> && !std::is_same_v<std::remove_cv_t<T>, wchar_t>;

}  // namespace detail
}  // namespace metl
