// EXPECT-ERROR: try_parse_int produces a SIGNED value
//
// The mirror of the try_parse_uint case: asking the signed reader for an
// unsigned target would make the sign handling and the range check disagree
// about the same field. Choosing the reader by the target type is what keeps
// "does this field accept a minus sign" answerable by reading the call.

#include <cstdint>

#include <metl/parse.hpp>
#include <metl/span.hpp>

auto control(metl::span<const char> text) {
  return metl::try_parse_int<std::int16_t>(text);
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::span<const char> text) {
  return metl::try_parse_int<std::uint16_t>(text);
}
#endif
