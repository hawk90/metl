// EXPECT-ERROR: metl::static_message_queue requires T to be nothrow destructible
//
// The queue destroys elements while unwinding its own storage. METL is a
// no-exception library: there is no handler above, so a destructor that throws
// reaches std::terminate. Requiring it at the type is the only point where a
// caller can still choose a different T.

#include <metl/static_message_queue.hpp>

namespace {

struct quiet {
  int value;
};

struct noisy {
  int value;
  ~noisy() noexcept(false) {}
};

}  // namespace

using control_queue = metl::static_message_queue<quiet, 4>;
static_assert(sizeof(control_queue) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender_queue = metl::static_message_queue<noisy, 4>;
static_assert(sizeof(offender_queue) > 0, "forces the instantiation");
#endif
