// EXPECT-WARNING: positional operator[] is removed in 2.0
//
// lookup_table's positional operator[] (and at) is deprecated for removal in 2.0 (#645): a use warns, naming
// the replacement, and the replacement builds without a warning.

#include <metl/lookup_table.hpp>

int control(const metl::lookup_table<int, int, 2>& t) {
  return t.nth(0).value;
}

#ifdef METL_COMPILE_FAIL
int offender(const metl::lookup_table<int, int, 2>& t) {
  return t[0].value;
}
#endif
