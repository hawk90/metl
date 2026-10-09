// EXPECT-ERROR: a METL container or vocabulary type holds objects
//
// void as the mapped type. It used to stop the build with errors from inside the header and no
// assertion; the class now refuses it as its first statement.

#include <cstdint>

#include <metl/flat_map.hpp>

using control = metl::flat_map<int, int, 4>;
static_assert(sizeof(control) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender = metl::flat_map<int, void, 4>;
static_assert(sizeof(offender) > 0, "forces the instantiation");
#endif
