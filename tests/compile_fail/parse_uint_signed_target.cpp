// EXPECT-ERROR: try_parse_uint produces an UNSIGNED value and rejects a leading '-'
//
// Parsing into a signed type through the unsigned reader is the mistake that
// reads correctly and is wrong: "-1" is rejected rather than parsed, so a field
// that legitimately carries a negative value silently becomes a parse error the
// caller reports as malformed input. The right call is try_parse_int, and the
// difference is invisible until a negative value actually arrives.

#include <cstdint>

#include <metl/parse.hpp>
#include <metl/span.hpp>

auto control(metl::span<const char> text) {
  return metl::try_parse_uint<std::uint16_t>(text);
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::span<const char> text) {
  return metl::try_parse_uint<std::int16_t>(text);
}
#endif
