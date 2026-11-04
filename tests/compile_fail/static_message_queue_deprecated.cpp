// EXPECT-WARNING: static_message_queue is removed in 2.0
//
// static_message_queue is deprecated for 2.0 (#166): a use warns, naming
// guarded<fixed_queue<T, N>, irq_lock>, and including the header alone does not.

#include <metl/fixed_queue.hpp>
#include <metl/static_message_queue.hpp>

bool control(metl::fixed_queue<int, 4>& q) {
  return q.try_push(1);
}

#ifdef METL_COMPILE_FAIL
bool offender(metl::static_message_queue<int, 4>& q) {
  return q.try_push(1);
}
#endif
