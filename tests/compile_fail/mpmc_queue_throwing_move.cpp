// EXPECT-ERROR: metl::mpmc_queue requires T to be nothrow move-constructible
//
// Same requirement as the SPSC ring, and worse if violated: the MPMC queue
// publishes a slot by advancing a sequence number AFTER the move. A throwing
// move leaves that slot claimed and never published, so every later producer
// and consumer spins on it -- a lock-free queue that has quietly deadlocked.

#include <metl/mpmc_queue.hpp>

namespace {

struct quiet {
  int value;
};

struct throwing_move {
  int value;
  throwing_move() = default;
  throwing_move(throwing_move&&) noexcept(false) {}
  throwing_move& operator=(throwing_move&&) noexcept(false) { return *this; }
};

}  // namespace

using control_queue = metl::mpmc_queue<quiet, 4>;
static_assert(sizeof(control_queue) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender_queue = metl::mpmc_queue<throwing_move, 4>;
static_assert(sizeof(offender_queue) > 0, "forces the instantiation");
#endif
