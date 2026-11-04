// EXPECT-WARNING: metl::is_clang is removed in 2.0
//
// The compiler.hpp inline variables #168 removes are deprecated for 2.0
// (#645): a use warns and names the macro to test instead.

#include <metl/compiler.hpp>

bool control() {
  return METL_COMPILER_CLANG != 0;
}

#ifdef METL_COMPILE_FAIL
bool offender() {
  return metl::is_clang;
}
#endif
