// EXPECT-ERROR: Capacity must be power of two
//
// spsc_queue masks instead of dividing, which is why the capacity has to be a
// power of two -- on a Cortex-M0 there is no divider at all, so a modulo here
// would be a libgcc call in the hot path. The static_assert is what tells a
// caller that, and it had never been verified to fire.

#include <metl/spsc_queue.hpp>

metl::spsc_queue<int, 8> control;

#ifdef METL_COMPILE_FAIL
metl::spsc_queue<int, 7> offender;
#endif
