// EXPECT-WARNING: task_count is removed in 2.0
//
// scheduler::task_count is deprecated for removal in 2.0 (#645): a use warns, naming the
// replacement, and the replacement builds without a warning.

#include <cstddef>

#include <metl/coro/scheduler.hpp>

std::size_t control(const metl::coro::scheduler<2>& s) {
  return s.size();
}

#ifdef METL_COMPILE_FAIL
std::size_t offender(const metl::coro::scheduler<2>& s) {
  return s.task_count();
}
#endif
