#include "metl/parse.hpp"

#include "metl_check.hpp"

#include "metl/format.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

/// A span over a string literal, WITHOUT its NUL. Every test here goes through
/// this, because the header's whole position is that the input is a span and not
/// a terminated string -- a test that quietly relied on a '\0' being one past
/// the end would be testing something the API does not promise.
constexpr metl::span<const char> text_of(const char* literal, std::size_t length) noexcept {
  return metl::span<const char>(literal, length);
}

#define TEXT(literal) text_of(literal, sizeof(literal) - 1)

template <typename T>
bool tail_is(const metl::parsed<T>& result, const char* expected) {
  const std::size_t length = std::strlen(expected);
  if (result.tail.size() != length) {
    return false;
  }
  for (std::size_t i = 0; i < length; ++i) {
    if (result.tail[i] != expected[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  // ---------------------------------------------------------------------
  // Unsigned decimal: value, tail, and the digit-count boundaries.
  // ---------------------------------------------------------------------
  {
    const auto zero = metl::try_parse_uint<std::uint32_t>(TEXT("0"));
    CHECK(zero.has_value());
    CHECK_EQ(zero->value, 0u);
    CHECK(zero->tail.empty());

    const auto plain = metl::try_parse_uint<std::uint32_t>(TEXT("12345"));
    CHECK(plain.has_value());
    CHECK_EQ(plain->value, 12345u);
    CHECK(plain->tail.empty());

    // Leading zeros are digits, not an error and not octal.
    const auto padded = metl::try_parse_uint<std::uint32_t>(TEXT("007"));
    CHECK(padded.has_value());
    CHECK_EQ(padded->value, 7u);

    // The tail is what makes scanning a line possible.
    const auto scanned = metl::try_parse_uint<std::uint16_t>(TEXT("21.5C"));
    CHECK(scanned.has_value());
    CHECK_EQ(scanned->value, 21u);
    CHECK(tail_is(*scanned, ".5C"));

    const auto full = metl::try_parse_uint<std::uint64_t>(TEXT("18446744073709551615"));
    CHECK(full.has_value());
    CHECK_EQ(full->value, std::numeric_limits<std::uint64_t>::max());
  }

  // ---------------------------------------------------------------------
  // Unsigned decimal: every way it can fail, each distinguishable.
  // ---------------------------------------------------------------------
  {
    const auto empty = metl::try_parse_uint<std::uint32_t>(metl::span<const char>());
    CHECK(!empty.has_value());
    CHECK(empty.error() == metl::parse_error::empty);

    const auto garbage = metl::try_parse_uint<std::uint32_t>(TEXT("abc"));
    CHECK(!garbage.has_value());
    CHECK(garbage.error() == metl::parse_error::not_a_number);

    // Whitespace is NOT skipped: a field with a space in front is a different
    // field, and the header says so.
    const auto spaced = metl::try_parse_uint<std::uint32_t>(TEXT(" 7"));
    CHECK(!spaced.has_value());
    CHECK(spaced.error() == metl::parse_error::not_a_number);

    // No sign is accepted for an unsigned field, not even '+'.
    const auto plus = metl::try_parse_uint<std::uint32_t>(TEXT("+7"));
    CHECK(!plus.has_value());
    CHECK(plus.error() == metl::parse_error::not_a_number);

    const auto minus = metl::try_parse_uint<std::uint32_t>(TEXT("-7"));
    CHECK(!minus.has_value());
    CHECK(minus.error() == metl::parse_error::not_a_number);
  }

  // ---------------------------------------------------------------------
  // Overflow is REPORTED, never wrapped. This is the property that makes the
  // type safe to point at a wire, so it is checked at each width's edge.
  // ---------------------------------------------------------------------
  {
    const auto max_u8 = metl::try_parse_uint<std::uint8_t>(TEXT("255"));
    CHECK(max_u8.has_value());
    CHECK_EQ(max_u8->value, std::uint8_t{255});

    const auto over_u8 = metl::try_parse_uint<std::uint8_t>(TEXT("256"));
    CHECK(!over_u8.has_value());
    CHECK(over_u8.error() == metl::parse_error::out_of_range);

    const auto max_u16 = metl::try_parse_uint<std::uint16_t>(TEXT("65535"));
    CHECK(max_u16.has_value());
    CHECK_EQ(max_u16->value, std::uint16_t{65535});

    const auto over_u16 = metl::try_parse_uint<std::uint16_t>(TEXT("65536"));
    CHECK(!over_u16.has_value());
    CHECK(over_u16.error() == metl::parse_error::out_of_range);

    const auto over_u64 = metl::try_parse_uint<std::uint64_t>(TEXT("18446744073709551616"));
    CHECK(!over_u64.has_value());
    CHECK(over_u64.error() == metl::parse_error::out_of_range);

    // Far past the end, so the fold has to survive many overflowing digits
    // rather than just one.
    const auto absurd = metl::try_parse_uint<std::uint32_t>(TEXT("999999999999999999999999999999"));
    CHECK(!absurd.has_value());
    CHECK(absurd.error() == metl::parse_error::out_of_range);
  }

  // ---------------------------------------------------------------------
  // Signed decimal, including the asymmetric minimum. `-min()` does not exist,
  // so this is the case a naive implementation gets wrong.
  // ---------------------------------------------------------------------
  {
    const auto positive = metl::try_parse_int<std::int32_t>(TEXT("42"));
    CHECK(positive.has_value());
    CHECK_EQ(positive->value, 42);

    const auto negative = metl::try_parse_int<std::int32_t>(TEXT("-42"));
    CHECK(negative.has_value());
    CHECK_EQ(negative->value, -42);

    const auto negative_zero = metl::try_parse_int<std::int32_t>(TEXT("-0"));
    CHECK(negative_zero.has_value());
    CHECK_EQ(negative_zero->value, 0);

    const auto min_i16 = metl::try_parse_int<std::int16_t>(TEXT("-32768"));
    CHECK(min_i16.has_value());
    CHECK_EQ(min_i16->value, std::numeric_limits<std::int16_t>::min());

    const auto max_i16 = metl::try_parse_int<std::int16_t>(TEXT("32767"));
    CHECK(max_i16.has_value());
    CHECK_EQ(max_i16->value, std::numeric_limits<std::int16_t>::max());

    // One past the positive edge, and one past the negative edge. The negative
    // limit is larger by one, so these two must NOT share a bound.
    const auto over_i16 = metl::try_parse_int<std::int16_t>(TEXT("32768"));
    CHECK(!over_i16.has_value());
    CHECK(over_i16.error() == metl::parse_error::out_of_range);

    const auto under_i16 = metl::try_parse_int<std::int16_t>(TEXT("-32769"));
    CHECK(!under_i16.has_value());
    CHECK(under_i16.error() == metl::parse_error::out_of_range);

    const auto min_i64 = metl::try_parse_int<std::int64_t>(TEXT("-9223372036854775808"));
    CHECK(min_i64.has_value());
    CHECK_EQ(min_i64->value, std::numeric_limits<std::int64_t>::min());

    // A lone sign is not a number.
    const auto lone = metl::try_parse_int<std::int32_t>(TEXT("-"));
    CHECK(!lone.has_value());
    CHECK(lone.error() == metl::parse_error::not_a_number);

    // '+' is refused, matching std::from_chars.
    const auto plus = metl::try_parse_int<std::int32_t>(TEXT("+42"));
    CHECK(!plus.has_value());
    CHECK(plus.error() == metl::parse_error::not_a_number);

    const auto with_tail = metl::try_parse_int<std::int32_t>(TEXT("-15,rest"));
    CHECK(with_tail.has_value());
    CHECK_EQ(with_tail->value, -15);
    CHECK(tail_is(*with_tail, ",rest"));
  }

  // ---------------------------------------------------------------------
  // Hex: both letter cases, no 0x prefix, overflow reported.
  // ---------------------------------------------------------------------
  {
    const auto lower = metl::try_parse_hex<std::uint32_t>(TEXT("deadbeef"));
    CHECK(lower.has_value());
    CHECK_EQ(lower->value, 0xdeadbeefu);

    const auto upper = metl::try_parse_hex<std::uint32_t>(TEXT("DEADBEEF"));
    CHECK(upper.has_value());
    CHECK_EQ(upper->value, 0xdeadbeefu);

    const auto mixed = metl::try_parse_hex<std::uint32_t>(TEXT("DeAdBeEf"));
    CHECK(mixed.has_value());
    CHECK_EQ(mixed->value, 0xdeadbeefu);

    // No prefix is accepted: "0x1f" parses the 0 and stops at 'x'.
    const auto prefixed = metl::try_parse_hex<std::uint32_t>(TEXT("0x1f"));
    CHECK(prefixed.has_value());
    CHECK_EQ(prefixed->value, 0u);
    CHECK(tail_is(*prefixed, "x1f"));

    // ...and stripping it yourself is the documented one-liner.
    const auto stripped = metl::try_parse_hex<std::uint32_t>(TEXT("0x1f").subspan(2));
    CHECK(stripped.has_value());
    CHECK_EQ(stripped->value, 0x1fu);

    const auto max_u16 = metl::try_parse_hex<std::uint16_t>(TEXT("ffff"));
    CHECK(max_u16.has_value());
    CHECK_EQ(max_u16->value, std::uint16_t{0xffff});

    const auto over_u16 = metl::try_parse_hex<std::uint16_t>(TEXT("10000"));
    CHECK(!over_u16.has_value());
    CHECK(over_u16.error() == metl::parse_error::out_of_range);

    const auto garbage = metl::try_parse_hex<std::uint32_t>(TEXT("ghi"));
    CHECK(!garbage.has_value());
    CHECK(garbage.error() == metl::parse_error::not_a_number);
  }

  // ---------------------------------------------------------------------
  // Round trip with metl/format.hpp. These are mirrors, so anything format
  // writes must parse back to the value it started from -- including the
  // boundaries each type gets wrong first.
  // ---------------------------------------------------------------------
  {
    char buf[32];
    const metl::span<char> out(buf, sizeof buf);

    const std::uint64_t unsigned_cases[] = {
        0u, 1u, 9u, 10u, 255u, 256u, 65535u, 65536u, 4294967295u, std::numeric_limits<std::uint64_t>::max()};
    for (std::uint64_t value : unsigned_cases) {
      const metl::span<char> written = metl::format_uint(out, value);
      const auto read =
          metl::try_parse_uint<std::uint64_t>(metl::span<const char>(written.data(), written.size()));
      CHECK(read.has_value());
      CHECK_EQ(read->value, value);
      CHECK(read->tail.empty());
    }

    const std::int64_t signed_cases[] = {0,
                                         1,
                                         -1,
                                         127,
                                         -128,
                                         32767,
                                         -32768,
                                         std::numeric_limits<std::int64_t>::max(),
                                         std::numeric_limits<std::int64_t>::min()};
    for (std::int64_t value : signed_cases) {
      const metl::span<char> written = metl::format_int(out, value);
      const auto read =
          metl::try_parse_int<std::int64_t>(metl::span<const char>(written.data(), written.size()));
      CHECK(read.has_value());
      CHECK_EQ(read->value, value);
      CHECK(read->tail.empty());
    }

    const std::uint32_t hex_cases[] = {0u, 1u, 0xfu, 0x10u, 0xdeadbeefu, 0xffffffffu};
    for (std::uint32_t value : hex_cases) {
      for (metl::hex_case letters : {metl::hex_case::lower, metl::hex_case::upper}) {
        const metl::span<char> written = metl::format_hex(out, value, 0, letters);
        const auto read =
            metl::try_parse_hex<std::uint32_t>(metl::span<const char>(written.data(), written.size()));
        CHECK(read.has_value());
        CHECK_EQ(read->value, value);
        CHECK(read->tail.empty());
      }
    }
  }

  // ---------------------------------------------------------------------
  // The asserting forms, on text the caller controls.
  // ---------------------------------------------------------------------
  {
    CHECK_EQ(metl::parse_uint<std::uint16_t>(TEXT("4095")).value, std::uint16_t{4095});
    CHECK_EQ(metl::parse_int<std::int16_t>(TEXT("-4095")).value, std::int16_t{-4095});
    CHECK_EQ(metl::parse_hex<std::uint16_t>(TEXT("0fff")).value, std::uint16_t{0x0fff});
  }

  // ---------------------------------------------------------------------
  // The arithmetic core IS constant-evaluable, even though the public
  // functions are not: they return metl::expected, whose storage is laundered
  // aligned storage rather than a union (expected.hpp says so at its
  // storage_union; docs/AUDIT.md Section A carries the rewrite). Testing the
  // fold at compile time means the day that rewrite lands, the part that has to
  // keep working is already covered.
  // ---------------------------------------------------------------------
  {
    constexpr bool folded = []() constexpr {
      const char digits[] = {'4', '0', '9', '5', 'z'};
      const auto run = metl::detail::fold_decimal(metl::span<const char>(digits, 5), 65535ULL);
      return run.value == 4095ULL && run.consumed == 4 && !run.overflowed;
    }();
    static_assert(folded, "the decimal fold must be usable in a constant expression");
    CHECK(folded);

    constexpr bool rejected = []() constexpr {
      const char digits[] = {'6', '5', '5', '3', '6'};
      const auto run = metl::detail::fold_decimal(metl::span<const char>(digits, 5), 65535ULL);
      return run.overflowed && run.consumed == 5;
    }();
    static_assert(rejected, "overflow must be detectable in a constant expression");
    CHECK(rejected);

    // Overflow keeps consuming digits, so `consumed` still spans the whole
    // number and a caller resuming at the tail does not restart mid-number.
    constexpr bool consumed_all = []() constexpr {
      const char digits[] = {'9', '9', '9', '9', '9', '9', ','};
      const auto run = metl::detail::fold_decimal(metl::span<const char>(digits, 7), 255ULL);
      return run.overflowed && run.consumed == 6;
    }();
    static_assert(consumed_all, "an overflowing run must still consume all of its digits");
    CHECK(consumed_all);

    constexpr bool hex_folded = []() constexpr {
      const char digits[] = {'f', 'F', '0', 'g'};
      const auto run = metl::detail::fold_hex(metl::span<const char>(digits, 4), 0xffffULL);
      return run.value == 0xff0ULL && run.consumed == 3 && !run.overflowed;
    }();
    static_assert(hex_folded, "the hex fold must be usable in a constant expression");
    CHECK(hex_folded);
  }

  return metl_test::exit_code();
}
