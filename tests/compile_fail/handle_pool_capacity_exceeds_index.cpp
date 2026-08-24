// EXPECT-ERROR: metl::handle_pool Capacity must fit the handle's slot index field
//
// The slot index is packed into a 16-bit field of the handle. A capacity past
// what that field addresses does not fail -- it TRUNCATES, so two different
// slots issue handles that compare equal, and the generation counter (the whole
// mechanism for detecting a stale handle) validates the wrong slot. A
// use-after-free that the type system was supposed to make impossible.

#include <metl/handle_pool.hpp>

using fits = metl::handle_pool<int, 8>;
static_assert(fits::max_capacity >= 8, "control instantiation");

#ifdef METL_COMPILE_FAIL
using overruns = metl::handle_pool<int, 65536>;  // max_index is 65535
static_assert(overruns::max_capacity != 0, "forces the instantiation");
#endif
