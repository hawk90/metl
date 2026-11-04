// EXPECT-WARNING: find_iter is removed in 2.0
//
// static_unordered_map::find_iter is deprecated for removal in 2.0 (#645): a use warns, naming the
// replacement, and the replacement builds without a warning.

#include <metl/static_unordered_map.hpp>

bool control(metl::static_unordered_map<int, int, 4>& m) {
  return m.find_iterator(1) == m.end();
}

#ifdef METL_COMPILE_FAIL
bool offender(metl::static_unordered_map<int, int, 4>& m) {
  return m.find_iter(1) == m.end();
}
#endif
