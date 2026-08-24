// EXPECT-ERROR: try_format_hex takes an integer: bool and the character types are excluded
//
// The third of the three writers, with the same exclusion and its own
// assertion. A `char` here is the most tempting mistake of the set -- hex of a
// byte is exactly what a caller wants, and `std::uint8_t` is the spelling that
// says so.

#include <cstdint>

#include <metl/format.hpp>
#include <metl/span.hpp>

auto control(metl::span<char> out) {
  return metl::try_format_hex(out, std::uint8_t{0x0F});
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::span<char> out) {
  return metl::try_format_hex(out, char{'\x0F'});
}
#endif
