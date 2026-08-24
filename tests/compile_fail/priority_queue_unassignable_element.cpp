// EXPECT-ERROR: fixed_priority_queue<T> requires a move-assignable T
//
// The other half of the sift. Move construction places an element in a fresh
// slot; move ASSIGNMENT overwrites an occupied one, which is what sifting down
// does on every pop. A type can easily satisfy one and not the other -- this
// one does -- so the two assertions are not redundant.

#include <metl/fixed_priority_queue.hpp>

namespace {

struct movable {
  int value;
  bool operator<(const movable& other) const { return value < other.value; }
};

struct unassignable {
  int value;
  unassignable(unassignable&&) = default;
  unassignable& operator=(unassignable&&) = delete;
  bool operator<(const unassignable& other) const { return value < other.value; }
};

}  // namespace

using control_queue = metl::fixed_priority_queue<movable, 4>;
static_assert(sizeof(control_queue) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender_queue = metl::fixed_priority_queue<unassignable, 4>;
static_assert(sizeof(offender_queue) > 0, "forces the instantiation");
#endif
