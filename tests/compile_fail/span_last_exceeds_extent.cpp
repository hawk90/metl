// EXPECT-ERROR: Count must not exceed Extent
//
// A static-extent span knows its length at compile time, so last<Count>() with
// too large a Count is answerable then rather than as an out-of-bounds read at
// run time. This is the assertion that makes the static extent worth having.

#include <metl/span.hpp>

int control(metl::span<const int, 4> values) {
  return values.last<2>()[0];
}

#ifdef METL_COMPILE_FAIL
int offender(metl::span<const int, 4> values) {
  return values.last<9>()[0];
}
#endif
