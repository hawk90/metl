// EXPECT-WARNING: macro 'METL_LIKELY' has been marked as deprecated
// WARNS-ON: clang
//
// The macros #168 removes are deprecated for 2.0 (#645). A macro cannot carry
// [[deprecated]]; clang 14+ warns at each expansion through
// `#pragma clang deprecated`, and GCC has no per-macro warning, hence WARNS-ON.

#include <metl/optimization.hpp>

int control(int x) {
  return METL_PREDICT_TRUE(x != 0) ? 1 : 0;
}

#ifdef METL_COMPILE_FAIL
int offender(int x) {
  return METL_LIKELY(x != 0) ? 1 : 0;
}
#endif
