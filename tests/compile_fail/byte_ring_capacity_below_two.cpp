// EXPECT-ERROR: spsc_byte_ring Capacity must be at least 2
//
// The byte ring is the one a driver ISR writes into. With a single byte of
// storage the read and write indices cannot differ, so `readable()` and
// `writable()` cannot both be meaningful and the ISR overwrites a byte the
// consumer has not taken -- at interrupt rate, with no test able to observe it.

#include <metl/spsc_byte_ring.hpp>

using two_bytes = metl::spsc_byte_ring<2>;
static_assert(sizeof(two_bytes) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using one_byte = metl::spsc_byte_ring<1>;
static_assert(sizeof(one_byte) > 0, "forces the instantiation");
#endif
