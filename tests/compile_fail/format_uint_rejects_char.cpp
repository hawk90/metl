// EXPECT-ERROR: try_format_uint takes an integer: bool and the character types are excluded
//
// `char` and `bool` are integral, so a permissive overload accepts them and
// prints a NUMBER: 'A' becomes "65" and `true` becomes "1". Both are plausible
// output that a reader takes at face value, which is why the character types
// are excluded at the type rather than handled by a runtime branch.

#include <cstdint>

#include <metl/format.hpp>
#include <metl/span.hpp>

auto control(metl::span<char> out) {
  return metl::try_format_uint(out, std::uint8_t{65});
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::span<char> out) {
  return metl::try_format_uint(out, char{'A'});
}
#endif
