// EXPECT-ERROR: try_parse_hex produces an integer: bool and the character types are excluded
//
// The hex reader's copy. Sharpest of the three: `try_parse_hex<char>` reads
// like "parse two hex digits into a byte", which is a thing callers genuinely
// want -- spelled `std::uint8_t`.

#include <cstdint>

#include <metl/parse.hpp>
#include <metl/span.hpp>

auto control(metl::span<const char> text) {
  return metl::try_parse_hex<std::uint8_t>(text);
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::span<const char> text) {
  return metl::try_parse_hex<char>(text);
}
#endif
