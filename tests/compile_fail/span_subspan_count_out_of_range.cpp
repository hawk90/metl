// EXPECT-ERROR: subspan<Offset, Count>(): Count out of range
//
// The companion to the Offset bound, and the easier one to get wrong: an
// in-range Offset with a Count that runs past the end reads like a valid
// window. `Count <= Extent - Offset`, not `Count <= Extent`.

#include <cstdint>

#include <metl/span.hpp>

using fixed = metl::span<const std::uint8_t, 8>;

auto control(fixed bytes) {
  return bytes.subspan<6, 2>();
}

#ifdef METL_COMPILE_FAIL
auto offender(fixed bytes) {
  return bytes.subspan<6, 3>();
}
#endif
