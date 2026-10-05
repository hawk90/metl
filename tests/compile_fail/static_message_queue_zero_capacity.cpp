// EXPECT-ERROR: metl::static_message_queue needs Capacity > 0
//
// A zero-capacity queue can hold nothing, and its back-reference arithmetic
// computes `Capacity - 1`, which wraps to SIZE_MAX: at METL_HARDENING_NONE,
// `emplace` returned a reference far outside the object (docs/AUDIT.md G.5).

#include <metl/static_message_queue.hpp>

using one_slot = metl::static_message_queue<int, 1>;
static_assert(sizeof(one_slot) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using no_slots = metl::static_message_queue<int, 0>;
static_assert(sizeof(no_slots) > 0, "forces the instantiation");
#endif
