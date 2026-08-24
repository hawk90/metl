// EXPECT-ERROR: metl::spsc_queue requires T to be nothrow move-constructible
//
// The consumer moves an element out of the ring and then advances the read
// index. If that move throws there is no state to roll back to -- METL has no
// exceptions, so it is std::terminate -- and if it were caught the element
// would be half-moved with the index already committed. The queue's whole
// progress guarantee assumes the move cannot fail.

#include <metl/spsc_queue.hpp>

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

using control_queue = metl::spsc_queue<quiet, 4>;
static_assert(sizeof(control_queue) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender_queue = metl::spsc_queue<throwing_move, 4>;
static_assert(sizeof(offender_queue) > 0, "forces the instantiation");
#endif
