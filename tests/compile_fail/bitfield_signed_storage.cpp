// EXPECT-ERROR: bitfield T must be unsigned
//
// Every bitfield operation is a shift and a mask. Shifting a signed value left
// past its sign bit is UB, and shifting one right is implementation-defined --
// so a signed storage type does not produce a wrong answer reliably, it
// produces a different wrong answer per compiler and optimisation level. The
// only place to refuse it is the type.

#include <cstdint>

#include <metl/bitfield.hpp>

using unsigned_storage = metl::bitfield<0, 4, std::uint32_t>;
static_assert(unsigned_storage::width == 4, "control instantiation");

#ifdef METL_COMPILE_FAIL
using signed_storage = metl::bitfield<0, 4, std::int32_t>;
static_assert(signed_storage::width == 4, "forces the instantiation");
#endif
