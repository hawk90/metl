// EXPECT-ERROR: try_parse_int produces an integer: bool and the character types are excluded
//
// The signed reader's copy of the exclusion -- a separate assertion, so a
// separate case. See parse_uint_rejects_char.cpp for the reasoning.

#include <cstdint>

#include <metl/parse.hpp>
#include <metl/span.hpp>

auto control(metl::span<const char> text) {
  return metl::try_parse_int<std::int8_t>(text);
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::span<const char> text) {
  return metl::try_parse_int<char>(text);
}
#endif
