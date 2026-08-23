// EXPECT-ERROR: mpmc_queue Capacity must be a power of two
//
// Same masking argument as spsc_queue, but a separate assertion with its own
// wording in a different header -- so it needs its own case. Two contracts that
// happen to agree are still two contracts.

#include <metl/mpmc_queue.hpp>

metl::mpmc_queue<int, 8> control;

#ifdef METL_COMPILE_FAIL
metl::mpmc_queue<int, 6> offender;
#endif
