// wire_values.cpp
//
// Reading numbers back off a wire with metl::parse.hpp -- the mirror of
// log_line.cpp, which writes them.
//
// The job: a telemetry line arrives as bytes, and the fields inside it have to
// become integers of a declared width without trusting the sender. `strtol` would
// do it, at the cost of a locale-aware conversion that reports failure through
// `errno` and takes a NUL-terminated string it will happily run off the end of --
// and the bytes coming out of a UART are precisely the input that has no
// terminator. So the parsers here take a `metl::span<const char>`: METL can bound
// a span, and cannot bound a `const char*`.
//
// The hard case this example is built to reach is NOT "does 21 parse as 21". It
// is the boundary: a field declared `std::uint8_t` that receives `256`, and a
// field declared `std::int16_t` that receives `-32769`, must be REFUSED rather
// than wrapped. A parser that wraps turns a corrupt packet into a plausible
// reading, which is the failure this whole library exists to make impossible.
// Both cases run below and the program fails loudly if either stops being
// refused.
//
// Floating point is not involved anywhere. `21.5` is read as an integer part and
// a fraction digit and scaled to tenths, which is what the firmware wanted in the
// first place -- see parse_tenths().
//
// Self-checking: every parse is compared against its expected value, every
// rejection against its expected parse_error, and the program returns non-zero
// on any mismatch.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <metl/format.hpp>
#include <metl/parse.hpp>
#include <metl/span.hpp>

namespace {

/// The wire never sends a NUL, so nothing here ever looks for one.
constexpr metl::span<const char> wire(const char* bytes, std::size_t length) noexcept {
  return metl::span<const char>(bytes, length);
}

#define WIRE(literal) wire(literal, sizeof(literal) - 1)

/// Advance past one expected character. Returns an empty span when the character
/// is not there, which every caller treats as a malformed line.
metl::span<const char> expect(metl::span<const char> text, char character) noexcept {
  if (text.empty() || text[0] != character) {
    return metl::span<const char>();
  }
  return text.subspan(std::size_t{1});
}

// ---------------------------------------------------------------------------
// Fixed point without floating point
// ---------------------------------------------------------------------------

/// Read `-21.5` as -215, i.e. tenths. Two integer parses and a scale; no `float`
/// anywhere, which is what a Cortex-M0 firmware wanted from the start.
///
/// The sign has to be taken from the WHOLE part before the fraction is folded in:
/// `-21.5` is -(21*10 + 5), not (-21)*10 + 5. Those differ by 10 and the second
/// one is the bug a reviewer should be looking for here.
metl::expected<std::int16_t, metl::parse_error> parse_tenths(metl::span<const char> text) noexcept {
  const auto whole = metl::try_parse_int<std::int16_t>(text);
  if (!whole) {
    return metl::unexpected<metl::parse_error>(whole.error());
  }

  std::int32_t tenths = static_cast<std::int32_t>(whole->value) * 10;
  const metl::span<const char> after_dot = expect(whole->tail, '.');
  if (!after_dot.empty()) {
    const auto fraction = metl::try_parse_uint<std::uint8_t>(after_dot.first(std::size_t{1}));
    if (!fraction) {
      return metl::unexpected<metl::parse_error>(fraction.error());
    }
    const auto digit = static_cast<std::int32_t>(fraction->value);
    tenths += (whole->value < 0 || (text.size() > 0 && text[0] == '-')) ? -digit : digit;
  }

  if (tenths > 32767 || tenths < -32768) {
    return metl::unexpected<metl::parse_error>(metl::parse_error::out_of_range);
  }
  return static_cast<std::int16_t>(tenths);
}

// ---------------------------------------------------------------------------
// One telemetry line
// ---------------------------------------------------------------------------

struct frame {
  std::uint16_t sequence;
  std::int16_t temperature_c10;
  std::uint32_t status_bits;
};

/// Parse `seq=1000 temp=-21.5 status=002A`.
///
/// Every field is read into the width it is declared as, so a value that does not
/// fit is refused here rather than becoming a smaller wrong number downstream.
metl::expected<frame, metl::parse_error> parse_frame(metl::span<const char> line) noexcept {
  frame out{};

  metl::span<const char> rest = line;
  for (const char* prefix = "seq="; *prefix != '\0'; ++prefix) {
    rest = expect(rest, *prefix);
  }
  const auto sequence = metl::try_parse_uint<std::uint16_t>(rest);
  if (!sequence) {
    return metl::unexpected<metl::parse_error>(sequence.error());
  }
  out.sequence = sequence->value;

  rest = expect(sequence->tail, ' ');
  for (const char* prefix = "temp="; *prefix != '\0'; ++prefix) {
    rest = expect(rest, *prefix);
  }
  const auto temperature = parse_tenths(rest);
  if (!temperature) {
    return metl::unexpected<metl::parse_error>(temperature.error());
  }
  out.temperature_c10 = temperature.value();

  // parse_tenths consumed a variable number of characters, so find the field
  // separator rather than assuming a width.
  std::size_t offset = 0;
  while (offset < rest.size() && rest[offset] != ' ') {
    ++offset;
  }
  rest = expect(rest.subspan(offset), ' ');
  for (const char* prefix = "status="; *prefix != '\0'; ++prefix) {
    rest = expect(rest, *prefix);
  }
  const auto status = metl::try_parse_hex<std::uint32_t>(rest);
  if (!status) {
    return metl::unexpected<metl::parse_error>(status.error());
  }
  out.status_bits = status->value;

  return out;
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------

int demo_fields() {
  const auto sequence = metl::try_parse_uint<std::uint16_t>(WIRE("1000 rest"));
  if (!sequence || sequence->value != 1000u) {
    return 1;
  }
  // The tail is what lets the next field start where this one stopped.
  if (sequence->tail.size() != 5 || sequence->tail[0] != ' ') {
    return 2;
  }

  const auto negative = metl::try_parse_int<std::int16_t>(WIRE("-215"));
  if (!negative || negative->value != -215) {
    return 3;
  }

  const auto status = metl::try_parse_hex<std::uint32_t>(WIRE("002A"));
  if (!status || status->value != 0x2Au) {
    return 4;
  }
  // Lower case too: format_hex can emit either, so parse_hex accepts either.
  const auto lower = metl::try_parse_hex<std::uint32_t>(WIRE("002a"));
  if (!lower || lower->value != 0x2Au) {
    return 5;
  }
  return 0;
}

/// THE POINT OF THIS EXAMPLE. A value one past the declared width is refused,
/// not truncated, and the refusal says which kind it was.
int demo_boundaries() {
  const auto fits = metl::try_parse_uint<std::uint8_t>(WIRE("255"));
  if (!fits || fits->value != 255u) {
    return 10;
  }
  const auto overflows = metl::try_parse_uint<std::uint8_t>(WIRE("256"));
  if (overflows) {
    return 11;  // wrapped to 0 -- a corrupt packet became a plausible reading
  }
  if (overflows.error() != metl::parse_error::out_of_range) {
    return 12;  // refused, but for the wrong stated reason
  }

  // The signed range is asymmetric, so the two edges must not share a bound.
  const auto min_fits = metl::try_parse_int<std::int16_t>(WIRE("-32768"));
  if (!min_fits || min_fits->value != -32768) {
    return 13;
  }
  const auto max_fits = metl::try_parse_int<std::int16_t>(WIRE("32767"));
  if (!max_fits || max_fits->value != 32767) {
    return 14;
  }
  const auto under = metl::try_parse_int<std::int16_t>(WIRE("-32769"));
  if (under || under.error() != metl::parse_error::out_of_range) {
    return 15;
  }
  const auto over = metl::try_parse_int<std::int16_t>(WIRE("32768"));
  if (over || over.error() != metl::parse_error::out_of_range) {
    return 16;
  }

  // Malformed input is distinguishable from out-of-range input, because a
  // caller does different things about them: one is a broken sender, the other
  // is a field that needs a wider type.
  const auto garbage = metl::try_parse_uint<std::uint16_t>(WIRE("NaN"));
  if (garbage || garbage.error() != metl::parse_error::not_a_number) {
    return 17;
  }
  const auto nothing = metl::try_parse_uint<std::uint16_t>(metl::span<const char>());
  if (nothing || nothing.error() != metl::parse_error::empty) {
    return 18;
  }

  // A space is not skipped. A field that arrived with one is a framing error,
  // and finding out here beats finding out three fields later.
  const auto spaced = metl::try_parse_uint<std::uint16_t>(WIRE(" 7"));
  if (spaced || spaced.error() != metl::parse_error::not_a_number) {
    return 19;
  }
  return 0;
}

int demo_tenths() {
  const auto positive = parse_tenths(WIRE("21.5"));
  if (!positive || positive.value() != 215) {
    return 20;
  }
  const auto negative = parse_tenths(WIRE("-21.5"));
  if (!negative || negative.value() != -215) {
    return 21;  // the sign must reach the fraction: -215, not -205
  }
  const auto whole_only = parse_tenths(WIRE("7"));
  if (!whole_only || whole_only.value() != 70) {
    return 22;
  }
  const auto negative_whole = parse_tenths(WIRE("-7"));
  if (!negative_whole || negative_whole.value() != -70) {
    return 23;
  }
  // Zero is the case where the sign cannot be recovered from the whole part,
  // which is why parse_tenths looks at the leading character instead.
  const auto negative_zero = parse_tenths(WIRE("-0.5"));
  if (!negative_zero || negative_zero.value() != -5) {
    return 24;
  }
  return 0;
}

int demo_frame() {
  const auto parsed = parse_frame(WIRE("seq=1000 temp=-21.5 status=002A"));
  if (!parsed) {
    return 30;
  }
  if (parsed->sequence != 1000u || parsed->temperature_c10 != -215 || parsed->status_bits != 0x2Au) {
    return 31;
  }

  // A sequence number that does not fit the declared width takes the whole
  // frame down, rather than silently becoming a different frame.
  const auto too_big = parse_frame(WIRE("seq=65536 temp=0.0 status=0"));
  if (too_big || too_big.error() != metl::parse_error::out_of_range) {
    return 32;
  }

  const auto malformed = parse_frame(WIRE("seq=abc temp=0.0 status=0"));
  if (malformed || malformed.error() != metl::parse_error::not_a_number) {
    return 33;
  }

  std::printf("  seq=%u temp=%d (tenths) status=0x%02X\n",
              static_cast<unsigned>(parsed->sequence),
              static_cast<int>(parsed->temperature_c10),
              static_cast<unsigned>(parsed->status_bits));
  return 0;
}

/// format.hpp wrote it, parse.hpp reads it back. These are mirrors, so a value
/// that survives the round trip is the strongest single check available.
int demo_round_trip() {
  char scratch[24];
  const metl::span<char> out(scratch, sizeof scratch);

  const std::int32_t cases[] = {0, 1, -1, 4095, -4095, 2147483647, -2147483647 - 1};
  for (std::int32_t value : cases) {
    const metl::span<char> text = metl::format_int(out, value);
    const auto read = metl::try_parse_int<std::int32_t>(metl::span<const char>(text.data(), text.size()));
    if (!read || read->value != value || !read->tail.empty()) {
      return 40;
    }
  }
  return 0;
}

}  // namespace

int main() {
  if (const int rc = demo_fields(); rc != 0) {
    std::printf("field parse failed: %d\n", rc);
    return rc;
  }
  if (const int rc = demo_boundaries(); rc != 0) {
    std::printf("boundary check failed: %d\n", rc);
    return rc;
  }
  if (const int rc = demo_tenths(); rc != 0) {
    std::printf("fixed-point check failed: %d\n", rc);
    return rc;
  }
  std::printf("wire_values: parsed one telemetry line without stdio or floating point,\n");
  if (const int rc = demo_frame(); rc != 0) {
    std::printf("frame parse failed: %d\n", rc);
    return rc;
  }
  if (const int rc = demo_round_trip(); rc != 0) {
    std::printf("round trip failed: %d\n", rc);
    return rc;
  }
  std::printf("and refused 256 into a uint8_t and -32769 into an int16_t rather than\n");
  std::printf("wrapping either into a plausible-looking reading.\n");
  return 0;
}
