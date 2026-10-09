// EXPECT-ERROR: a METL container or vocabulary type holds objects
//
// A pool of references has nothing to construct. It used to stop the build with errors from inside the header
// and no assertion; the class now refuses it as its first statement.

#include <cstdint>

#include <metl/object_pool.hpp>

using control = metl::object_pool<std::uint32_t, 4>;
static_assert(sizeof(control) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender = metl::object_pool<std::uint32_t&, 4>;
static_assert(sizeof(offender) > 0, "forces the instantiation");
#endif
