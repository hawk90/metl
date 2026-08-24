// EXPECT-ERROR: fixed_priority_queue<T> requires a move-constructible T
//
// Restoring the heap invariant after a push or a pop is a sift, and a sift
// MOVES elements. Without move construction the container would fall back to
// copying, which for a type that deleted its move on purpose (a scoped
// resource, a handle with unique ownership) is either wrong or does not exist.

#include <metl/fixed_priority_queue.hpp>

namespace {

struct movable {
  int value;
  bool operator<(const movable& other) const { return value < other.value; }
};

struct immovable {
  int value;
  immovable() = default;
  immovable(immovable&&) = delete;
  immovable& operator=(immovable&&) = delete;
  bool operator<(const immovable& other) const { return value < other.value; }
};

}  // namespace

using control_queue = metl::fixed_priority_queue<movable, 4>;
static_assert(sizeof(control_queue) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender_queue = metl::fixed_priority_queue<immovable, 4>;
static_assert(sizeof(offender_queue) > 0, "forces the instantiation");
#endif
