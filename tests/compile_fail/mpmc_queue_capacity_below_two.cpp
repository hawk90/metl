// EXPECT-ERROR: mpmc_queue Capacity must be at least 2
//
// Same reason as the SPSC ring one file over, and worth pinning separately
// because it is a separate assertion in a separate header: with a single slot
// there is no gap between head and tail to distinguish empty from full, so the
// sequence-number protocol that makes the queue lock-free has nothing to
// compare against.

#include <metl/mpmc_queue.hpp>

using two_slots = metl::mpmc_queue<int, 2>;
static_assert(sizeof(two_slots) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using one_slot = metl::mpmc_queue<int, 1>;
static_assert(sizeof(one_slot) > 0, "forces the instantiation");
#endif
