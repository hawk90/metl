// EXPECT-ERROR: spsc_byte_ring Capacity must be a power of two
//
// The byte ring wraps with a mask on every push and pop; a non-power-of-two
// capacity would silently corrupt the wrap rather than merely being slow.

#include <metl/spsc_byte_ring.hpp>

metl::spsc_byte_ring<16> control;

#ifdef METL_COMPILE_FAIL
metl::spsc_byte_ring<12> offender;
#endif
