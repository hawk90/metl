// EXPECT-ERROR: try_format_uint takes an UNSIGNED value
//
// A negative signed value converted to unsigned is a huge positive number, so
// -1 would print as 4294967295 rather than failing. The output is a plausible
// reading of a counter that has simply gone the other way.

#include <cstdint>

#include <metl/format.hpp>
#include <metl/span.hpp>

auto control(metl::span<char> out) {
  return metl::try_format_uint(out, std::uint32_t{5});
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::span<char> out) {
  return metl::try_format_uint(out, std::int32_t{-1});
}
#endif
