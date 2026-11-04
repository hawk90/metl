// EXPECT-WARNING: positional at is removed in 2.0
//
// flat_set's positional at (and operator[]) is deprecated for removal in 2.0 (#645): a use warns, naming the
// replacement, and the replacement builds without a warning.

#include <metl/flat_set.hpp>

int control(const metl::flat_set<int, 4>& s) {
  return s.nth(0);
}

#ifdef METL_COMPILE_FAIL
int offender(const metl::flat_set<int, 4>& s) {
  return s.at(0);
}
#endif
