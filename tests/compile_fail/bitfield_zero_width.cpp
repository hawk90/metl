// EXPECT-ERROR: bitfield Width must be > 0
//
// A zero-width field has no bits to carry a value, and the mask it computes is
// either 0 (every write silently discarded) or, if the ones-value is built by
// shifting, a shift by the full width of T -- which is UB. Neither shows up as
// a failure at runtime; the register simply never changes.

#include <cstdint>

#include <metl/bitfield.hpp>

using one_bit = metl::bitfield<3, 1, std::uint32_t>;
static_assert(one_bit::width == 1, "control instantiation");

#ifdef METL_COMPILE_FAIL
using no_bits = metl::bitfield<3, 0, std::uint32_t>;
static_assert(no_bits::lsb == 3, "forces the instantiation");
#endif
