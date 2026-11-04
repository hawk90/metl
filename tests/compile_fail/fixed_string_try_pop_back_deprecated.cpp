// EXPECT-WARNING: try_pop_back is removed in 2.0
//
// fixed_string::try_pop_back is deprecated for removal in 2.0 (#645): a use warns, naming the
// replacement, and the replacement builds without a warning.

#include <metl/fixed_string.hpp>

void control(metl::fixed_string<4>& s) {
  if (!s.empty()) {
    s.pop_back();
  }
}

#ifdef METL_COMPILE_FAIL
bool offender(metl::fixed_string<4>& s) {
  return s.try_pop_back();
}
#endif
