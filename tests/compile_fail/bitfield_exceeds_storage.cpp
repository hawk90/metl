// EXPECT-ERROR: bitfield Lsb + Width exceeds storage size
//
// A field that runs off the end of its storage word is the one bitfield mistake
// that does not announce itself: the mask is computed by shifting, so an
// over-wide field silently shifts past the width of T and the register write
// lands on nothing. This assertion is the whole defence, and it is off-by-one
// prone -- `Lsb + Width <= bits`, not `<`.

#include <cstdint>

#include <metl/bitfield.hpp>

using fits = metl::bitfield<24, 8, std::uint32_t>;  // exactly reaches bit 31
static_assert(fits::width == 8, "control instantiation");

#ifdef METL_COMPILE_FAIL
using overruns = metl::bitfield<24, 16, std::uint32_t>;  // wants bits 24..39
static_assert(overruns::width == 16, "forces the instantiation");
#endif
