// EXPECT-ERROR: emplace<T> requires unique alternative type
//
// With the same type listed twice, emplace<T> has to choose an index, and
// whichever it chose would be arbitrary. The variant would then report an
// active alternative the caller did not name, and a later get<T> -- equally
// ambiguous -- could disagree.

#include <metl/variant.hpp>

void control(metl::variant<int, long>& value) {
  value.emplace<long>(7L);
}

#ifdef METL_COMPILE_FAIL
void offender(metl::variant<int, int>& value) {
  value.emplace<int>(7);
}
#endif
