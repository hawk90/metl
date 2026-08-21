#pragma once

/// @file
/// @brief Progress guarantees for the bounded parsers (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | every function in this header | wait-free, bounded by the input span |
///
/// A parse reads at most one character per iteration and stops at the first
/// character that cannot extend the number, so the worst case is the length of
/// the span you pass -- and a 64-bit value runs out of digits long before a
/// realistic buffer runs out of characters.
///
/// The input is a `metl::span<const char>` and never a `const char*`, which is
/// the one design decision in this header worth stating as a guarantee. The
/// `const char*` overloads on `metl::hash`, `metl::fixed_string` and the CRC
/// headers are bounded only by the caller's NUL terminator; METL cannot bound
/// them, and a buffer without a terminator reads past its end. Text coming off a
/// wire is exactly the input that has no terminator, so the parsers refuse to
/// take one.

// Bounded text-to-integer conversion: the mirror of metl/format.hpp.
//
// The gap this fills is the same one, read backwards. `metl/format.hpp` turns a
// number into characters for a log line or a protocol field; nothing in
// `include/metl` turned characters back into a number, so a caller receiving
// `TEMP=21` over a UART had `<cstdlib>`'s `strtol` (locale-aware, sets `errno`,
// and takes a NUL-terminated string it will happily run off the end of),
// `<charconv>` (which METL does not include and will not start including), or a
// hand-rolled loop in every project. `examples/uart_byte_ring.cpp` already
// assembles such a line and had no way to read the number out of it.
//
// DELIBERATELY NOT A SCANF. There is no format string here, no `%d`, and no
// whitespace skipping. A protocol field with a space in front of it is a
// different field, and silently accepting one is the kind of surprise
// docs/SCOPE.md forbids. What is here is the small set embedded parsing needs:
// unsigned decimal, signed decimal, and hex.
//
// FLOATING POINT IS NOT HERE, and is not an oversight. `metl/format.hpp` does
// not format floats either -- correctly-rounded conversion is a table and a lot
// of code in every image that links it, and most MCU firmware that reads `21.5`
// off a wire wants fixed-point anyway. Parse the integer part and the fraction
// digits separately and scale; `examples/wire_values.cpp` shows the four lines
// that takes.
//
// Composes with the rest of the library through spans it already produces:
//
//     const auto line = ring.readable_span();          // spsc_byte_ring
//     const auto parsed = metl::try_parse_uint<std::uint16_t>(line);
//
// NOT CONSTANT-EVALUABLE TODAY, and this is the one place parse.hpp does not
// mirror format.hpp. `try_format_uint` returns a `span`, which is a literal
// type, so a table of text can be built at compile time. These return
// `metl::expected`, whose storage is laundered aligned storage rather than a
// union of {T, E} -- `expected.hpp` says so at its `storage_union`, and
// docs/AUDIT.md Section A carries the rewrite as a deferred item. The
// `constexpr` labels below are for the same reason `expected`'s own are: they
// cost nothing, they document intent, and they start working the day that
// rewrite lands. Until then a config table is parsed at startup, not at build
// time. The arithmetic core in `detail` IS constant-evaluable and is tested that
// way, so the rewrite will not be changing untested code.

#include "metl/assert.hpp"
#include "metl/config.hpp"
#include "metl/detail/plain_integer.hpp"
#include "metl/expected.hpp"
#include "metl/span.hpp"

#include <cstddef>
#include <limits>
#include <type_traits>

namespace metl {

/// @brief Why a parse did not produce a number.
enum class parse_error : unsigned char {
  /// The span was empty. Distinguished from `not_a_number` because "nothing
  /// arrived yet" and "garbage arrived" are different things to a caller
  /// draining a stream.
  empty,
  /// The span was not empty but does not start with a number: no digit, or a
  /// `-` with no digit after it.
  not_a_number,
  /// The digits are a number, but not one `T` can hold.
  out_of_range,
};

/// @brief A parsed value and the characters that followed it.
///
/// @tparam T The integer type that was parsed.
///
/// `tail` is what scanning a stream needs: after reading `21` out of `21.5`,
/// `tail` is `.5`. To require that the whole span was a number and nothing else,
/// check `tail.empty()` -- that is one line at the call site and avoids a second
/// set of `_exact` overloads.
template <typename T>
struct parsed {
  T value;                ///< The number.
  span<const char> tail;  ///< Everything after the last character consumed.
};

namespace detail {

constexpr bool is_decimal_digit(char character) noexcept {
  return character >= '0' && character <= '9';
}

/// Hex digit value, or 16 for "not a hex digit". Both letter cases are accepted,
/// because `metl::format_hex` can emit either and a parser that rejected the
/// output of its own mirror would be indefensible.
constexpr unsigned hex_digit_value(char character) noexcept {
  if (character >= '0' && character <= '9') {
    return static_cast<unsigned>(character - '0');
  }
  if (character >= 'a' && character <= 'f') {
    return static_cast<unsigned>(character - 'a') + 10u;
  }
  if (character >= 'A' && character <= 'F') {
    return static_cast<unsigned>(character - 'A') + 10u;
  }
  return 16u;
}

/// Outcome of folding a run of digits.
struct digit_run {
  unsigned long long value;
  std::size_t consumed;
  bool overflowed;
};

/// Fold decimal digits from the front of @p text, stopping at the first
/// non-digit, and report whether the run exceeded @p limit.
///
/// The overflow test is `value > limit / 10 || (value == limit / 10 && digit >
/// limit % 10)` rather than the tempting `value * 10 + digit > limit`, which
/// cannot work: the product it needs to compare has already wrapped by the time
/// the comparison runs. Digits keep being consumed after an overflow so that
/// `consumed` still describes the whole number -- a caller that stopped early
/// would resume parsing in the middle of one.
constexpr digit_run fold_decimal(span<const char> text, unsigned long long limit) noexcept {
  unsigned long long value = 0;
  std::size_t index = 0;
  bool overflowed = false;
  const unsigned long long limit_head = limit / 10ULL;
  const unsigned long long limit_tail = limit % 10ULL;

  while (index < text.size() && is_decimal_digit(text[index])) {
    const auto digit = static_cast<unsigned long long>(text[index] - '0');
    if (value > limit_head || (value == limit_head && digit > limit_tail)) {
      overflowed = true;
    } else {
      value = value * 10ULL + digit;
    }
    ++index;
  }
  return digit_run{value, index, overflowed};
}

/// Fold hex digits from the front of @p text. Same shape as `fold_decimal`; the
/// overflow test is a shift rather than a division because the base is a power
/// of two.
constexpr digit_run fold_hex(span<const char> text, unsigned long long limit) noexcept {
  unsigned long long value = 0;
  std::size_t index = 0;
  bool overflowed = false;
  const unsigned long long limit_head = limit / 16ULL;
  const unsigned long long limit_tail = limit % 16ULL;

  while (index < text.size() && hex_digit_value(text[index]) < 16u) {
    const auto digit = static_cast<unsigned long long>(hex_digit_value(text[index]));
    if (value > limit_head || (value == limit_head && digit > limit_tail)) {
      overflowed = true;
    } else {
      value = value * 16ULL + digit;
    }
    ++index;
  }
  return digit_run{value, index, overflowed};
}

/// The largest magnitude a negative `T` can represent, computed without ever
/// negating the minimum -- `-std::numeric_limits<T>::min()` is undefined, which
/// is the same hazard `metl/format.hpp`'s `magnitude_of` exists to avoid.
template <typename T>
constexpr unsigned long long negative_limit_of() noexcept {
  using unsigned_t = std::make_unsigned_t<T>;
  return static_cast<unsigned long long>(static_cast<unsigned_t>(std::numeric_limits<T>::max())) + 1ULL;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Unsigned decimal
// ---------------------------------------------------------------------------

/// @brief Read an unsigned decimal number from the front of @p text.
/// @tparam T The unsigned integer type to produce; must be named explicitly.
/// @param text Characters to read. Not modified, and not required to be
///        NUL-terminated.
/// @return The value and the characters after it, or a `parse_error`.
/// @note No sign is accepted, not even `+`. `std::from_chars` takes the same
///       position, and for an unsigned field a `-` is a protocol error the
///       caller should see rather than a value silently wrapped.
/// @note Leading whitespace is **not** skipped and leading zeros **are**
///       accepted: `007` is 7, and ` 7` is `not_a_number`.
/// @note Overflow is reported, never wrapped. `try_parse_uint<std::uint8_t>` on
///       `"256"` is `out_of_range`, not `0`.
template <typename T>
METL_NODISCARD constexpr expected<parsed<T>, parse_error> try_parse_uint(span<const char> text) noexcept {
  static_assert(detail::is_plain_integer_v<T>,
                "try_parse_uint produces an integer: bool and the character types are excluded "
                "because reading them as numbers is almost never what was meant");
  static_assert(std::is_unsigned_v<T>,
                "try_parse_uint produces an UNSIGNED value and rejects a leading '-'. Use "
                "try_parse_int for a field that may be negative");

  if (text.empty()) {
    return unexpected<parse_error>(parse_error::empty);
  }
  const auto limit = static_cast<unsigned long long>(std::numeric_limits<T>::max());
  const detail::digit_run run = detail::fold_decimal(text, limit);
  if (run.consumed == 0) {
    return unexpected<parse_error>(parse_error::not_a_number);
  }
  if (run.overflowed) {
    return unexpected<parse_error>(parse_error::out_of_range);
  }
  return parsed<T>{static_cast<T>(run.value), text.subspan(run.consumed)};
}

/// @brief Read an unsigned decimal number from the front of @p text.
/// @tparam T The unsigned integer type to produce.
/// @param text Characters to read.
/// @return The value and the characters after it.
/// @pre @p text starts with a number that fits in `T`; anything else asserts and
///      aborts.
/// @warning **Use this only on text you produced or control** — a table of
///          compile-time constants, or something `metl::format_uint` just wrote.
///          Text off a wire is untrusted by definition, and turning a malformed
///          packet into a reset is not error handling. That case is what
///          `try_parse_uint` is for.
template <typename T>
constexpr parsed<T> parse_uint(span<const char> text) noexcept {
  const expected<parsed<T>, parse_error> result = try_parse_uint<T>(text);
  METL_ASSERT(result.has_value());
  return result.value();
}

// ---------------------------------------------------------------------------
// Signed decimal
// ---------------------------------------------------------------------------

/// @brief Read a signed decimal number from the front of @p text.
/// @tparam T The signed integer type to produce; must be named explicitly.
/// @param text Characters to read.
/// @return The value and the characters after it, or a `parse_error`.
/// @note A leading `-` is accepted; a leading `+` is **not**, matching
///       `std::from_chars`. `"-"` alone is `not_a_number`.
/// @note The most negative value parses correctly. The magnitude is folded in
///       the unsigned domain and only then given its sign, so `"-32768"` into an
///       `std::int16_t` is `-32768` and not an overflow report.
template <typename T>
METL_NODISCARD constexpr expected<parsed<T>, parse_error> try_parse_int(span<const char> text) noexcept {
  static_assert(detail::is_plain_integer_v<T>,
                "try_parse_int produces an integer: bool and the character types are excluded "
                "because reading them as numbers is almost never what was meant");
  static_assert(std::is_signed_v<T>,
                "try_parse_int produces a SIGNED value. Use try_parse_uint for an unsigned "
                "field, so that a leading '-' is refused rather than wrapped");

  if (text.empty()) {
    return unexpected<parse_error>(parse_error::empty);
  }
  const bool negative = text[0] == '-';
  const span<const char> digits = negative ? text.subspan(std::size_t{1}) : text;

  const unsigned long long limit = negative ? detail::negative_limit_of<T>()
                                            : static_cast<unsigned long long>(std::numeric_limits<T>::max());
  const detail::digit_run run = detail::fold_decimal(digits, limit);
  if (run.consumed == 0) {
    return unexpected<parse_error>(parse_error::not_a_number);
  }
  if (run.overflowed) {
    return unexpected<parse_error>(parse_error::out_of_range);
  }

  const span<const char> tail = digits.subspan(run.consumed);
  if (!negative) {
    return parsed<T>{static_cast<T>(run.value), tail};
  }
  // run.value is at most max()+1. Negating max() is fine; the extra one is the
  // minimum, which is returned directly because -min() does not exist.
  if (run.value == detail::negative_limit_of<T>()) {
    return parsed<T>{std::numeric_limits<T>::min(), tail};
  }
  return parsed<T>{static_cast<T>(-static_cast<T>(run.value)), tail};
}

/// @brief Read a signed decimal number from the front of @p text.
/// @tparam T The signed integer type to produce.
/// @param text Characters to read.
/// @return The value and the characters after it.
/// @pre @p text starts with a number that fits in `T`; anything else asserts and
///      aborts.
/// @warning Same warning as `parse_uint`: for text you control only. Untrusted
///          input goes through `try_parse_int`.
template <typename T>
constexpr parsed<T> parse_int(span<const char> text) noexcept {
  const expected<parsed<T>, parse_error> result = try_parse_int<T>(text);
  METL_ASSERT(result.has_value());
  return result.value();
}

// ---------------------------------------------------------------------------
// Hex
// ---------------------------------------------------------------------------

/// @brief Read an unsigned hex number from the front of @p text.
/// @tparam T The unsigned integer type to produce; must be named explicitly.
/// @param text Characters to read.
/// @return The value and the characters after it, or a `parse_error`.
/// @note **No `0x` prefix is accepted**, exactly as `metl::format_hex` writes
///       none. A caller framing a protocol field must not have one appear, and a
///       caller that wants one strips it: `text.subspan(2)`.
/// @note Both letter cases are accepted, because `format_hex` can emit either.
/// @note No sign is accepted; hex fields are bit patterns.
template <typename T>
METL_NODISCARD constexpr expected<parsed<T>, parse_error> try_parse_hex(span<const char> text) noexcept {
  static_assert(detail::is_plain_integer_v<T>,
                "try_parse_hex produces an integer: bool and the character types are excluded "
                "because reading them as numbers is almost never what was meant");
  static_assert(std::is_unsigned_v<T>,
                "try_parse_hex produces an UNSIGNED value; a hex field is a bit pattern. Cast "
                "to the signed type whose width you actually mean");

  if (text.empty()) {
    return unexpected<parse_error>(parse_error::empty);
  }
  const auto limit = static_cast<unsigned long long>(std::numeric_limits<T>::max());
  const detail::digit_run run = detail::fold_hex(text, limit);
  if (run.consumed == 0) {
    return unexpected<parse_error>(parse_error::not_a_number);
  }
  if (run.overflowed) {
    return unexpected<parse_error>(parse_error::out_of_range);
  }
  return parsed<T>{static_cast<T>(run.value), text.subspan(run.consumed)};
}

/// @brief Read an unsigned hex number from the front of @p text.
/// @tparam T The unsigned integer type to produce.
/// @param text Characters to read.
/// @return The value and the characters after it.
/// @pre @p text starts with a hex number that fits in `T`; anything else asserts
///      and aborts.
/// @warning Same warning as `parse_uint`: for text you control only. Untrusted
///          input goes through `try_parse_hex`.
template <typename T>
constexpr parsed<T> parse_hex(span<const char> text) noexcept {
  const expected<parsed<T>, parse_error> result = try_parse_hex<T>(text);
  METL_ASSERT(result.has_value());
  return result.value();
}

}  // namespace metl
