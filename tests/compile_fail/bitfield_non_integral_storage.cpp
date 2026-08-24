// EXPECT-ERROR: bitfield T must be integral
//
// The first of bitfield's four preconditions, and the one that decides whether
// the other three are even meaningful: a shift and a mask are defined for
// integers. The near-miss worth refusing here is a floating-point T, which has
// a size and an alignment and looks storage-shaped right up to the first shift.

#include <cstdint>

#include <metl/bitfield.hpp>

using integral_storage = metl::bitfield<0, 4, std::uint32_t>;
static_assert(integral_storage::width == 4, "control instantiation");

#ifdef METL_COMPILE_FAIL
using float_storage = metl::bitfield<0, 4, float>;
static_assert(float_storage::width == 4, "forces the instantiation");
#endif
