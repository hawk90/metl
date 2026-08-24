// EXPECT-ERROR: variant requires at least one alternative
//
// `variant<>` has no valid state: there is no alternative for index 0, so a
// default constructor cannot establish the invariant every other member relies
// on. Without this assertion the type is constructible and every access is a
// read of uninitialised storage through a type nothing chose.

#include <metl/variant.hpp>

using one = metl::variant<int>;
static_assert(sizeof(one) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using none = metl::variant<>;
static_assert(sizeof(none) > 0, "forces the instantiation");
#endif
