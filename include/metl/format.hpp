#pragma once

/// @file
/// @brief Progress guarantees for the bounded formatters (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | every function in this header | wait-free, bounded by the output span |
///
/// Formatting an integer costs one iteration per digit produced, and the number of
/// digits is bounded twice over: by the type (20 for a 64-bit value in decimal, 16
/// in hex) and by the size of the span you pass. Nothing here scans an input
/// string, allocates, or consults a locale, so there is no path whose cost depends
/// on anything but the value and the buffer.
///
/// A buffer too small to hold the result is a capacity failure like any other: the
/// `try_` form returns `nullopt` and the asserting form aborts. Neither writes past
/// the span.

// Bounded integer-to-text conversion for logs, diagnostics and protocol framing.
//
// The gap this fills: `fixed_string` can hold text but has no way to put a NUMBER
// into it, so the options today are `snprintf` (drags in stdio, allocates on some
// libcs, and its return value lies about truncation in ways people get wrong) or a
// hand-rolled loop in every project. Nothing in `include/metl` uses `<charconv>` or
// `<cstdio>`, and this keeps it that way.
//
// DELIBERATELY NOT A FORMAT-STRING LIBRARY. There is no `format("{}: {}", ...)`
// here and there is not going to be one: a format-string parser is code and tables
// in the image of every target that links it, for an ergonomic gain that a few
// explicit calls already deliver. What is here is the small set that embedded
// diagnostics actually need -- unsigned decimal, signed decimal, and hex with
// optional zero padding.
//
// THE BUFFER IS THE CALLER'S, on purpose. A member function that formatted into a
// hidden scratch buffer would have to make it either `static` (not reentrant, and
// this library is used from ISRs) or a stack temporary the caller cannot size. An
// explicit `char buf[N]` is visible, sized by the caller, and safe to use from
// anywhere.
//
// Composes with `fixed_string` through the API it already has:
//
//     char scratch[24];
//     line.try_append(metl::format_uint(scratch, reading));

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/span.hpp"

#include <cstddef>
#include <type_traits>

namespace metl {

/// @brief Letter case for the hex digits `a`-`f`.
enum class hex_case : unsigned char {
  lower,  ///< `0x1f3a`
  upper,  ///< `0x1F3A`
};

namespace detail {

/// Decimal digit count of an unsigned magnitude; 0 has one digit.
constexpr std::size_t decimal_digits(unsigned long long magnitude) noexcept {
  std::size_t digits = 1;
  while (magnitude >= 10ULL) {
    magnitude /= 10ULL;
    ++digits;
  }
  return digits;
}

/// Hex digit count of an unsigned value; 0 has one digit.
constexpr std::size_t hex_digits(unsigned long long value) noexcept {
  std::size_t digits = 1;
  while (value >= 16ULL) {
    value >>= 4U;
    ++digits;
  }
  return digits;
}

/// Writes `count` decimal digits of `magnitude` backwards, ending at `out[count-1]`.
constexpr void write_decimal(span<char> out, std::size_t count, unsigned long long magnitude) noexcept {
  std::size_t index = count;
  while (index > 0) {
    --index;
    out[index] = static_cast<char>('0' + static_cast<char>(magnitude % 10ULL));
    magnitude /= 10ULL;
  }
}

/// Writes `count` hex digits of `value` backwards, zero-padded on the left.
constexpr void write_hex(span<char> out,
                         std::size_t count,
                         unsigned long long value,
                         hex_case letters) noexcept {
  const char alphabet_offset = (letters == hex_case::upper) ? 'A' : 'a';
  std::size_t index = count;
  while (index > 0) {
    --index;
    const auto nibble = static_cast<unsigned>(value & 0xFULL);
    out[index] = nibble < 10U ? static_cast<char>('0' + static_cast<char>(nibble))
                              : static_cast<char>(alphabet_offset + static_cast<char>(nibble - 10U));
    value >>= 4U;
  }
}

/// The magnitude of a signed value, computed in the unsigned domain so that the
/// most negative value does not overflow. `-value` on LLONG_MIN is undefined; this
/// is the standard way around it and is why the signed path is not just a negate.
constexpr unsigned long long magnitude_of(long long value) noexcept {
  const auto unsigned_value = static_cast<unsigned long long>(value);
  return value < 0 ? (0ULL - unsigned_value) : unsigned_value;
}

template <typename T>
constexpr bool is_plain_integer_v =
    std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> &&
    !std::is_same_v<std::remove_cv_t<T>, char> && !std::is_same_v<std::remove_cv_t<T>, char16_t> &&
    !std::is_same_v<std::remove_cv_t<T>, char32_t> && !std::is_same_v<std::remove_cv_t<T>, wchar_t>;

}  // namespace detail

// ---------------------------------------------------------------------------
// Unsigned decimal
// ---------------------------------------------------------------------------

/// @brief Write @p value as decimal into @p out, if it fits.
/// @param out Destination characters.
/// @param value The unsigned value to render.
/// @return The text written, or an **empty span** if @p out is too small — in which
///         case @p out is not modified at all.
/// @note An empty span is unambiguous as the failure marker: a number is always at
///       least one character, so a successful call never returns one. Same
///       convention as `spsc_byte_ring`'s spans.
/// @note The result is NOT NUL-terminated. It is a span, and its size is the
///       length; feed it to `fixed_string::try_append(span<const char>)` or write
///       it straight out.
template <typename T>
METL_NODISCARD constexpr span<char> try_format_uint(span<char> out, T value) noexcept {
  static_assert(detail::is_plain_integer_v<T>,
                "try_format_uint takes an integer: bool and the character types are excluded "
                "because printing them as numbers is almost never what was meant");
  static_assert(std::is_unsigned_v<T>,
                "try_format_uint takes an UNSIGNED value; a negative signed value would convert "
                "to a huge positive one and print as such. Use try_format_int instead");
  const auto magnitude = static_cast<unsigned long long>(value);
  const std::size_t count = detail::decimal_digits(magnitude);
  if (count > out.size()) {
    return out.first(std::size_t{0});
  }
  detail::write_decimal(out, count, magnitude);
  return out.first(count);
}

/// @brief Write @p value as decimal into @p out.
/// @param out Destination characters.
/// @param value The unsigned value to render.
/// @return The text written.
/// @pre It fits; too small a buffer asserts and aborts. Use `try_format_uint` where
///      the buffer size is not known to be sufficient.
/// @note 20 characters hold any 64-bit unsigned value, so a `char[20]` makes this
///       the right form: the precondition is then a compile-time-obvious fact
///       rather than a runtime check on every call.
template <typename T>
constexpr span<char> format_uint(span<char> out, T value) noexcept {
  const span<char> text = try_format_uint(out, value);
  METL_ASSERT(!text.empty());
  return text;
}

// ---------------------------------------------------------------------------
// Signed decimal
// ---------------------------------------------------------------------------

/// @brief Write @p value as decimal into @p out, with a leading `-` when negative.
/// @param out Destination characters.
/// @param value The signed value to render.
/// @return The text written, or an empty span if @p out is too small (@p out
///         unmodified).
template <typename T>
METL_NODISCARD constexpr span<char> try_format_int(span<char> out, T value) noexcept {
  static_assert(detail::is_plain_integer_v<T>,
                "try_format_int takes an integer: bool and the character types are excluded "
                "because printing them as numbers is almost never what was meant");
  const auto widened = static_cast<long long>(value);
  const unsigned long long magnitude = detail::magnitude_of(widened);
  const std::size_t digits = detail::decimal_digits(magnitude);
  const std::size_t count = digits + (widened < 0 ? 1U : 0U);
  if (count > out.size()) {
    return out.first(std::size_t{0});
  }
  if (widened < 0) {
    out[0] = '-';
    detail::write_decimal(out.subspan(1), digits, magnitude);
  } else {
    detail::write_decimal(out, digits, magnitude);
  }
  return out.first(count);
}

/// @brief Write @p value as decimal into @p out.
/// @param out Destination characters.
/// @param value The signed value to render.
/// @return The text written.
/// @pre It fits; too small a buffer asserts and aborts. A `char[20]` holds any
///      64-bit signed value including the sign.
template <typename T>
constexpr span<char> format_int(span<char> out, T value) noexcept {
  const span<char> text = try_format_int(out, value);
  METL_ASSERT(!text.empty());
  return text;
}

// ---------------------------------------------------------------------------
// Hex
// ---------------------------------------------------------------------------

/// @brief Write @p value as hex into @p out, if it fits.
/// @param out Destination characters.
/// @param value The unsigned value to render.
/// @param digits Exact number of digits, zero-padded on the left. `0` means "as
///        many as the value needs". A register or a byte reads better at a fixed
///        width, which is why this is a parameter rather than a separate function.
/// @param letters Lower or upper case for `a`-`f`.
/// @return The text written, or an empty span if @p out is too small, **or if
///         @p digits is too few to hold the value** — a silently truncated register
///         dump is worse than no dump.
/// @note No `0x` prefix: callers that want one write it, and callers framing a
///       protocol field must not have one appear.
template <typename T>
METL_NODISCARD constexpr span<char> try_format_hex(span<char> out,
                                                   T value,
                                                   std::size_t digits = 0,
                                                   hex_case letters = hex_case::lower) noexcept {
  static_assert(detail::is_plain_integer_v<T>,
                "try_format_hex takes an integer: bool and the character types are excluded "
                "because printing them as numbers is almost never what was meant");
  static_assert(std::is_unsigned_v<T>,
                "try_format_hex takes an UNSIGNED value; a negative signed value would be "
                "sign-extended to a 64-bit pattern of f's. Cast to the unsigned type whose "
                "width you actually mean");
  const auto widened = static_cast<unsigned long long>(value);
  const std::size_t needed = detail::hex_digits(widened);
  const std::size_t count = digits == 0 ? needed : digits;
  if (count > out.size() || needed > count) {
    return out.first(std::size_t{0});
  }
  detail::write_hex(out, count, widened, letters);
  return out.first(count);
}

/// @brief Write @p value as hex into @p out.
/// @param out Destination characters.
/// @param value The unsigned value to render.
/// @param digits Exact number of digits, zero-padded; `0` means as many as needed.
/// @param letters Lower or upper case for `a`-`f`.
/// @return The text written.
/// @pre It fits and @p digits is wide enough; a violation asserts and aborts.
template <typename T>
constexpr span<char> format_hex(span<char> out,
                                T value,
                                std::size_t digits = 0,
                                hex_case letters = hex_case::lower) noexcept {
  const span<char> text = try_format_hex(out, value, digits, letters);
  METL_ASSERT(!text.empty());
  return text;
}

}  // namespace metl
