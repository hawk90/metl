// EXPECT-ERROR: a METL container or vocabulary type holds objects
//
// A function type is not an object. It used to stop the build with errors from inside the header and no
// assertion; the class now refuses it as its first statement.

#include <cstdint>

#include <metl/fixed_deque.hpp>

using control = metl::fixed_deque<int, 4>;
static_assert(sizeof(control) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender = metl::fixed_deque<void(), 4>;
static_assert(sizeof(offender) > 0, "forces the instantiation");
#endif
