// EXPECT-ERROR: subspan<Offset, Count>(): Offset must not exceed Extent
//
// A subspan past the end is a pointer past the end and a length taken on trust.
// Nothing dereferences it at the point of the mistake, so the out-of-bounds
// read happens later, in the caller's loop, attributed to code that is correct.

#include <cstdint>

#include <metl/span.hpp>

using fixed = metl::span<const std::uint8_t, 8>;

auto control(fixed bytes) {
  return bytes.subspan<4, 2>();
}

#ifdef METL_COMPILE_FAIL
auto offender(fixed bytes) {
  return bytes.subspan<9, 1>();
}
#endif
