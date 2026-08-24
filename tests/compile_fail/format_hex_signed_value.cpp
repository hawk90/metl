// EXPECT-ERROR: try_format_hex takes an UNSIGNED value
//
// A hex field is a bit pattern, and a negative signed value is sign-extended
// before it is printed -- so -1 as an int8_t formats as FFFFFFFF rather than
// FF. The output is well-formed hex of the wrong width, which is exactly the
// kind of wrong a log line does not reveal.

#include <cstdint>

#include <metl/format.hpp>
#include <metl/span.hpp>

auto control(metl::span<char> out) {
  return metl::try_format_hex(out, std::uint8_t{0xAB});
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::span<char> out) {
  return metl::try_format_hex(out, std::int8_t{-1});
}
#endif
