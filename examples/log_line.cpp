// log_line.cpp
//
// Building diagnostic lines on a target with no stdio, using metl::format.hpp
// plus the fixed_string API that already exists.
//
// The job: turn sensor state into a line a UART can send, with a bounded buffer,
// no allocation, and no silent truncation. `snprintf` would do it, at the cost of
// pulling stdio into the image (and on some libcs, malloc with it), and with a
// return value that reports what WOULD have been written rather than what was --
// which is how truncation gets missed.
//
// There is deliberately no format string here. `format("{} {}", a, b)` needs a
// parser in the image of every target that links it; a few explicit calls do the
// same work with none.
//
// Self-checking: every line is compared against its exact expected text, and the
// program returns non-zero on any mismatch.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <metl/fixed_string.hpp>
#include <metl/format.hpp>

namespace {

using line_type = metl::fixed_string<64>;

struct reading {
  std::uint16_t sequence;
  std::int16_t temperature_c10;  // tenths of a degree, e.g. -215 == -21.5 C
  std::uint32_t status_bits;
};

/// The scratch buffer is the CALLER's, on purpose: a hidden one inside the
/// formatter would have to be `static` (not reentrant -- and this kind of code
/// runs from ISRs) or a stack temporary the caller cannot size. 24 characters
/// hold any 64-bit value in decimal or hex, sign included.
char g_scratch[24];

/// Appends a decimal integer. `append` (rather than `try_append`) is right here
/// because the line buffer is sized for the worst case; overflow would mean the
/// sizing is wrong and should abort rather than silently drop characters.
void append_int(line_type& line, long long value) {
  line.append(metl::span<const char>(metl::format_int(metl::span<char>(g_scratch, sizeof g_scratch), value)));
}

void append_uint(line_type& line, unsigned long long value) {
  line.append(
      metl::span<const char>(metl::format_uint(metl::span<char>(g_scratch, sizeof g_scratch), value)));
}

/// Fixed-width hex is what a register or a status word wants: `0x002a` lines up
/// in a log where `0x2a` does not, and a too-narrow width is refused rather than
/// truncated.
void append_hex(line_type& line, unsigned long long value, std::size_t digits) {
  line.append(metl::span<const char>(
      metl::format_hex(metl::span<char>(g_scratch, sizeof g_scratch), value, digits, metl::hex_case::upper)));
}

/// "-21.5" from -215, without floating point: the integer and fractional parts
/// are formatted separately. Fixed-point is how embedded code carries this.
void append_tenths(line_type& line, std::int16_t tenths) {
  if (tenths < 0) {
    line.append("-");
  }
  const int magnitude = tenths < 0 ? -static_cast<int>(tenths) : static_cast<int>(tenths);
  append_int(line, magnitude / 10);
  line.append(".");
  append_int(line, magnitude % 10);
}

line_type render(const reading& sample) {
  line_type line;
  line.append("seq=");
  append_uint(line, sample.sequence);
  line.append(" temp=");
  append_tenths(line, sample.temperature_c10);
  line.append("C status=0x");
  append_hex(line, sample.status_bits, 4);
  return line;
}

}  // namespace

int main() {
  const reading samples[] = {
      {1, 215, 0x002Au},
      {2, -215, 0x0000u},
      {3, 0, 0xFFFFu},
      {42, -5, 0x0100u},
  };
  const char* const expected[] = {
      "seq=1 temp=21.5C status=0x002A",
      "seq=2 temp=-21.5C status=0x0000",
      "seq=3 temp=0.0C status=0xFFFF",
      "seq=42 temp=-0.5C status=0x0100",
  };

  for (std::size_t i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
    const line_type line = render(samples[i]);
    if (!(line == line_type(expected[i]))) {
      std::printf("line %zu: got \"%s\", want \"%s\"\n", i, line.c_str(), expected[i]);
      return 1;
    }
    std::printf("  %s\n", line.c_str());
  }

  // Truncation is reported, never silent. A 16-character line cannot hold this,
  // and try_append says so instead of writing a prefix.
  {
    metl::fixed_string<16> tiny;
    tiny.append("value=");
    char scratch[24];
    const metl::span<char> text =
        metl::format_uint(metl::span<char>(scratch, sizeof scratch), 18446744073709551615ULL);
    if (tiny.try_append(metl::span<const char>(text))) {
      return 2;  // 6 + 20 characters cannot fit in 16
    }
    if (!(tiny == metl::fixed_string<16>("value="))) {
      return 3;  // and the refusal must leave the string exactly as it was
    }
  }

  // A too-narrow fixed hex width is refused rather than dropping the high
  // nibbles -- a register dump missing its top half is worse than none.
  {
    char scratch[8];
    if (!metl::try_format_hex(metl::span<char>(scratch, sizeof scratch), 0xABCDu, 2).empty()) {
      return 4;
    }
  }

  std::printf("log_line: 4 lines rendered with no stdio formatting and no allocation\n");
  return 0;
}
