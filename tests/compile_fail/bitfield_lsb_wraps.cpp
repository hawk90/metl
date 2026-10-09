// EXPECT-ERROR: bitfield Lsb + Width exceeds storage size
//
// An Lsb so large that `Lsb + Width` wraps size_t to a small number. The
// check was written as that sum, so it passed; it is now written so it cannot
// wrap.

#include <cstddef>
#include <cstdint>

#include <metl/bitfield.hpp>

using control = metl::bitfield<30, 2, std::uint32_t>;
static_assert(control::width == 2, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender = metl::bitfield<static_cast<std::size_t>(-1), 2, std::uint32_t>;
static_assert(offender::width == 2, "forces the instantiation");
#endif
