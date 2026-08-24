// EXPECT-ERROR: try_parse_uint produces an integer: bool and the character types are excluded
//
// The reader's half of the same exclusion the writers make. Parsing into a
// `char` target would read "65" and produce 'A', or read "1" and produce a
// `bool` -- either way the caller gets a value whose type says character and
// whose contents came from a number.

#include <cstdint>

#include <metl/parse.hpp>
#include <metl/span.hpp>

auto control(metl::span<const char> text) {
  return metl::try_parse_uint<std::uint8_t>(text);
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::span<const char> text) {
  return metl::try_parse_uint<char>(text);
}
#endif
