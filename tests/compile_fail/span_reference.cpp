// EXPECT-ERROR: span requires an object element type
//
// span<int&> is not a view of anything: the element type must be an object
// type. Arrays are allowed as elements, so span has its own assertion.

#include <metl/span.hpp>

using control = metl::span<int>;
static_assert(sizeof(control) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender = metl::span<int&>;
static_assert(sizeof(offender) > 0, "forces the instantiation");
#endif
