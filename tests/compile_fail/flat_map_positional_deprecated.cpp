// EXPECT-WARNING: positional operator[] is removed in 2.0
//
// flat_map's positional operator[] (and at) is deprecated for removal in 2.0 (#645): a use warns, naming the
// replacement, and the replacement builds without a warning.

#include <metl/flat_map.hpp>

int control(const metl::flat_map<int, int, 4>& m) {
  return m.nth(0).value;
}

#ifdef METL_COMPILE_FAIL
int offender(const metl::flat_map<int, int, 4>& m) {
  return m[0].value;
}
#endif
