// EXPECT-ERROR: spsc_queue Capacity must be at least 2
//
// A lock-free ring distinguishes empty from full by the gap between head and
// tail. With one slot there is no gap to hold that distinction, so `empty()`
// and `full()` answer the same thing and the producer overwrites an unread
// element with no test able to see it happen on a single-threaded run.

#include <metl/spsc_queue.hpp>

using two_slots = metl::spsc_queue<int, 2>;
static_assert(two_slots::capacity() == 2, "control instantiation");

#ifdef METL_COMPILE_FAIL
using one_slot = metl::spsc_queue<int, 1>;
static_assert(one_slot::capacity() != 0, "forces the instantiation");
#endif
