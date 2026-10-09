// EXPECT-ERROR: a METL container or vocabulary type holds objects
//
// An array alternative cannot be assigned or returned. It used to stop the build with errors from inside the
// header and no assertion; the class now refuses it as its first statement.

#include <cstdint>

#include <metl/variant.hpp>

using control = metl::variant<int, long>;
static_assert(sizeof(control) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender = metl::variant<int, int[3]>;
static_assert(sizeof(offender) > 0, "forces the instantiation");
#endif
