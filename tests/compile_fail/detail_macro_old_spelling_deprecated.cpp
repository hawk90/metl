// EXPECT-WARNING: macro 'METL_CONSTEXPR20' has been marked as deprecated
// WARNS-ON: clang
//
// The internal helpers #168 renames to METL_DETAIL_* keep their old spelling
// in 1.x as a deprecated alias (#645). clang 14+ warns at each expansion; GCC
// has no per-macro warning, hence WARNS-ON.

#include <metl/detail/construct.hpp>

constexpr int control() {
  return 1;
}

#ifdef METL_COMPILE_FAIL
METL_CONSTEXPR20 int offender() {
  return 1;
}
#endif
