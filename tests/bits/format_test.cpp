#include "metl/format.hpp"

#include "metl_check.hpp"

#include "metl/fixed_string.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

/// Compare a returned span against an expected C string, checking the LENGTH too:
/// the result is not NUL-terminated, so a comparison that stopped at a '\0' would
/// pass on a buffer that happened to contain one.
bool text_is(metl::span<char> actual, const char* expected) {
  const std::size_t length = std::strlen(expected);
  if (actual.size() != length) {
    return false;
  }
  for (std::size_t i = 0; i < length; ++i) {
    if (actual[i] != expected[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  char buf[32];
  const metl::span<char> out(buf, sizeof buf);

  // ---------------------------------------------------------------------
  // Unsigned decimal, including the boundaries where the digit count changes.
  // ---------------------------------------------------------------------
  {
    CHECK(text_is(metl::format_uint(out, 0u), "0"));
    CHECK(text_is(metl::format_uint(out, 7u), "7"));
    CHECK(text_is(metl::format_uint(out, 9u), "9"));
    CHECK(text_is(metl::format_uint(out, 10u), "10"));
    CHECK(text_is(metl::format_uint(out, 99u), "99"));
    CHECK(text_is(metl::format_uint(out, 100u), "100"));
    CHECK(text_is(metl::format_uint(out, 4294967295u), "4294967295"));
    CHECK(text_is(metl::format_uint(out, ULLONG_MAX), "18446744073709551615"));
    // 20 characters is the widest a 64-bit unsigned can be, which is what makes
    // `char[20]` the documented safe size for the asserting form.
    CHECK_EQ(metl::format_uint(out, ULLONG_MAX).size(), 20u);
  }

  // ---------------------------------------------------------------------
  // Signed decimal. LLONG_MIN is the case a naive `-value` gets wrong: negating
  // it overflows, which is undefined. The magnitude is taken in the unsigned
  // domain instead.
  //
  // Note what these assertions can and cannot do: on two's-complement hardware
  // the naive `-value` happens to produce the RIGHT number, so the checks below
  // pass either way. What actually guards that line is UBSan -- mutating it back
  // makes the `sanitizers` job report "negation of -9223372036854775808 cannot be
  // represented in type 'long long'". Verified by doing exactly that. So do not
  // read these as covering the undefined-behaviour half; the sanitizer job does.
  // ---------------------------------------------------------------------
  {
    CHECK(text_is(metl::format_int(out, 0), "0"));
    CHECK(text_is(metl::format_int(out, 42), "42"));
    CHECK(text_is(metl::format_int(out, -1), "-1"));
    CHECK(text_is(metl::format_int(out, -9), "-9"));
    CHECK(text_is(metl::format_int(out, -10), "-10"));
    CHECK(text_is(metl::format_int(out, LLONG_MAX), "9223372036854775807"));
    CHECK(text_is(metl::format_int(out, LLONG_MIN), "-9223372036854775808"));
    CHECK_EQ(metl::format_int(out, LLONG_MIN).size(), 20u);

    // Narrower signed types widen correctly rather than wrapping.
    CHECK(text_is(metl::format_int(out, static_cast<std::int8_t>(-128)), "-128"));
    CHECK(text_is(metl::format_int(out, static_cast<std::int16_t>(-32768)), "-32768"));
    CHECK(text_is(metl::format_int(out, static_cast<std::int32_t>(-2147483647 - 1)), "-2147483648"));
  }

  // ---------------------------------------------------------------------
  // Hex, free width and fixed width.
  // ---------------------------------------------------------------------
  {
    CHECK(text_is(metl::format_hex(out, 0u), "0"));
    CHECK(text_is(metl::format_hex(out, 0x1fu), "1f"));
    CHECK(text_is(metl::format_hex(out, 0x1fu, 0, metl::hex_case::upper), "1F"));
    CHECK(text_is(metl::format_hex(out, 0xdeadbeefu), "deadbeef"));
    CHECK(text_is(metl::format_hex(out, 0xdeadbeefu, 0, metl::hex_case::upper), "DEADBEEF"));
    CHECK(text_is(metl::format_hex(out, ULLONG_MAX), "ffffffffffffffff"));
    CHECK_EQ(metl::format_hex(out, ULLONG_MAX).size(), 16u);

    // Fixed width zero-pads on the left — the point of the parameter.
    CHECK(text_is(metl::format_hex(out, 0x5u, 2), "05"));
    CHECK(text_is(metl::format_hex(out, 0x5u, 8), "00000005"));
    CHECK(text_is(metl::format_hex(out, 0u, 4), "0000"));
    CHECK(text_is(metl::format_hex(out, 0xabcdu, 4), "abcd"));
    // Exactly wide enough is not an error.
    CHECK(text_is(metl::format_hex(out, 0xabcdu, 4, metl::hex_case::upper), "ABCD"));
  }

  // ---------------------------------------------------------------------
  // Too small a buffer: empty span, and NOTHING is written.
  // ---------------------------------------------------------------------
  {
    char small[3];
    for (char& c : small) {
      c = '#';
    }
    const metl::span<char> tight(small, sizeof small);

    // 4 digits into 3 characters.
    CHECK(metl::try_format_uint(tight, 1234u).empty());
    CHECK_EQ(small[0], '#');  // untouched
    CHECK_EQ(small[1], '#');
    CHECK_EQ(small[2], '#');

    // The sign counts toward the width: "-99" fits exactly, "-100" does not.
    CHECK(text_is(metl::try_format_int(tight, -99), "-99"));
    for (char& c : small) {
      c = '#';
    }
    CHECK(metl::try_format_int(tight, -100).empty());
    CHECK_EQ(small[0], '#');

    // A zero-length destination refuses everything, including "0".
    const metl::span<char> nothing(small, std::size_t{0});
    CHECK(metl::try_format_uint(nothing, 0u).empty());
    CHECK(metl::try_format_int(nothing, 0).empty());
    CHECK(metl::try_format_hex(nothing, 0u).empty());
  }

  // ---------------------------------------------------------------------
  // Too FEW fixed hex digits is a refusal, not a truncation. A register dump
  // that silently drops its high nibbles is worse than no dump.
  // ---------------------------------------------------------------------
  {
    CHECK(metl::try_format_hex(out, 0xabcdu, 3).empty());
    CHECK(metl::try_format_hex(out, 0xabcdu, 1).empty());
    CHECK(text_is(metl::try_format_hex(out, 0xabcdu, 4), "abcd"));
    CHECK(text_is(metl::try_format_hex(out, 0xabcdu, 5), "0abcd"));
    // ...and asking for more digits than the buffer holds is also a refusal.
    char eight[8];
    CHECK(metl::try_format_hex(metl::span<char>(eight, sizeof eight), 0x1u, 9).empty());
  }

  // ---------------------------------------------------------------------
  // The composition this exists for: building a log line in a fixed_string
  // using only the API fixed_string already has.
  // ---------------------------------------------------------------------
  {
    metl::fixed_string<64> line;
    char scratch[24];
    const metl::span<char> pad(scratch, sizeof scratch);

    line.append("temp=");
    line.append(metl::span<const char>(metl::format_int(pad, -215)));
    line.append(" rssi=0x");
    line.append(metl::span<const char>(metl::format_hex(pad, 0x2au, 4, metl::hex_case::upper)));
    line.append(" seq=");
    line.append(metl::span<const char>(metl::format_uint(pad, 1000u)));

    CHECK(line == metl::fixed_string<64>("temp=-215 rssi=0x002A seq=1000"));
  }

  // ---------------------------------------------------------------------
  // Constant-evaluable, so a table of text can be built at compile time.
  // ---------------------------------------------------------------------
  {
    constexpr bool folded = []() constexpr {
      char storage[8] = {};
      const metl::span<char> target(storage, 8);
      const metl::span<char> text = metl::try_format_uint(target, 4095u);
      return text.size() == 4 && storage[0] == '4' && storage[1] == '0' && storage[2] == '9' &&
             storage[3] == '5';
    }();
    static_assert(folded, "try_format_uint must be usable in a constant expression");
    CHECK(folded);
  }

  return metl_test::exit_code();
}
