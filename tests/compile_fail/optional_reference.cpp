// EXPECT-ERROR: a METL container or vocabulary type holds objects
//
// A reference: optional<T&> is not supported (a pointer is). It used to stop the build with errors from
// inside the header and no assertion; the class now refuses it as its first statement.

#include <cstdint>

#include <metl/optional.hpp>

using control = metl::optional<int>;
static_assert(sizeof(control) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender = metl::optional<int&>;
static_assert(sizeof(offender) > 0, "forces the instantiation");
#endif
