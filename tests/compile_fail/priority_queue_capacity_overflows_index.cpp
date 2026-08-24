// EXPECT-ERROR: fixed_priority_queue Capacity is too large: the child index 2*i+1 would
//
// A heap addresses children as 2*i+1, so the largest index it computes is
// 2*Capacity-1. Past the point where that wraps, the sift walks to a small
// index instead of off the end -- it does not crash, it silently reorders the
// wrong elements, and the queue keeps answering with a plausible top().

#include <cstddef>

#include <metl/fixed_priority_queue.hpp>

using reasonable = metl::fixed_priority_queue<int, 8>;
static_assert(sizeof(reasonable) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
constexpr std::size_t too_large = (static_cast<std::size_t>(-1) - 1) / 2 + 1;
using overflows = metl::fixed_priority_queue<int, too_large>;
static_assert(sizeof(overflows) > 0, "forces the instantiation");
#endif
