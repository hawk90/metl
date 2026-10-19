// EXPECT-ERROR: first<Count>(): Count must not exceed Extent
//
// The first<Count>() half of the static-extent check. span_last_exceeds_extent
// fires last<Count>()'s assertion; the two messages share their tail, so that
// case alone could not show that this one still fires (#407).

#include <metl/span.hpp>

int control(metl::span<const int, 4> values) {
  return values.first<2>()[0];
}

#ifdef METL_COMPILE_FAIL
int offender(metl::span<const int, 4> values) {
  return values.first<9>()[0];
}
#endif
