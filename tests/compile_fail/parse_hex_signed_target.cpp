// EXPECT-ERROR: try_parse_hex produces an UNSIGNED value
//
// A hex field is a bit pattern, and reading "FFFF" into an int16_t asks the
// parser to decide whether that is 65535 (out of range) or -1 (the same bits).
// Either answer is defensible and neither is what every caller meant, so the
// reader produces the bits and the caller casts deliberately.

#include <cstdint>

#include <metl/parse.hpp>
#include <metl/span.hpp>

auto control(metl::span<const char> text) {
  return metl::try_parse_hex<std::uint16_t>(text);
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::span<const char> text) {
  return metl::try_parse_hex<std::int16_t>(text);
}
#endif
