// EXPECT-ERROR: T must be constructible from the given arguments
//
// emplace constructs in place, so the arguments have to reach a constructor.
// Without the check the failure would surface as an instantiation backtrace
// through the storage machinery, several frames from the call the caller wrote
// -- which is the exact experience this library's static_asserts exist to
// replace.

#include <metl/variant.hpp>

namespace {

struct needs_two {
  needs_two(int, int) {}
};

}  // namespace

using holder = metl::variant<int, needs_two>;

void control(holder& value) {
  value.emplace<needs_two>(1, 2);
}

#ifdef METL_COMPILE_FAIL
void offender(holder& value) {
  value.emplace<needs_two>(1);
}
#endif
