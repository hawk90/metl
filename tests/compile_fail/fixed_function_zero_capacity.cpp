// EXPECT-ERROR: fixed_function Capacity must be at least 1
//
// A zero-byte buffer can hold no target. Without the check the error was
// "no matching function for call to zero_for_constant_init" (#592).

#include <metl/fixed_function.hpp>

using one_byte = metl::fixed_function<void(), 1>;
static_assert(sizeof(one_byte) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using zero_bytes = metl::fixed_function<void(), 0>;
static_assert(sizeof(zero_bytes) > 0, "forces the instantiation");
#endif
