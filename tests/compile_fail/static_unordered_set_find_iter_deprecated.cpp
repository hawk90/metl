// EXPECT-WARNING: find_iter is removed in 2.0
//
// static_unordered_set::find_iter is deprecated for removal in 2.0 (#645): a use warns, naming the
// replacement, and the replacement builds without a warning.

#include <metl/static_unordered_set.hpp>

bool control(metl::static_unordered_set<int, 4>& s) {
  return s.find_iterator(1) == s.end();
}

#ifdef METL_COMPILE_FAIL
bool offender(metl::static_unordered_set<int, 4>& s) {
  return s.find_iter(1) == s.end();
}
#endif
