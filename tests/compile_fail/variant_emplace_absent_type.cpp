// EXPECT-ERROR: type is not an alternative of this variant
//
// emplace<T> for a T the variant does not list. The first of emplace's three
// checks, and the one a typo lands on: `emplace<uint32_t>` where the
// alternative is `int` is a different type on most targets and the same one on
// some, so without this the code would build on the developer's machine and
// not on the target -- or the reverse.

#include <metl/variant.hpp>

using pair = metl::variant<int, long>;

void control(pair& value) {
  value.emplace<long>(7L);
}

#ifdef METL_COMPILE_FAIL
void offender(pair& value) {
  value.emplace<char>('x');
}
#endif
