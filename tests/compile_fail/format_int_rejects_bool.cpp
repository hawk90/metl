// EXPECT-ERROR: try_format_int takes an integer: bool and the character types are excluded
//
// Same exclusion as try_format_uint, in the signed writer. `bool` is integral,
// so a permissive overload prints "1" for `true` -- which is a valid line of
// output that a log reader has no way to question.

#include <cstdint>

#include <metl/format.hpp>
#include <metl/span.hpp>

auto control(metl::span<char> out) {
  return metl::try_format_int(out, std::int16_t{-5});
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::span<char> out) {
  return metl::try_format_int(out, true);
}
#endif
