// EXPECT-ERROR: bitfield T must not be bool
//
// `bool` passes `is_unsigned`, but a bitfield clears its bits with `~mask`, and
// `~mask` converted back to bool is always `true`. `insert(true, false)` then
// returned 1: the field could be set and never cleared (docs/AUDIT.md G.5).

#include <cstdint>

#include <metl/bitfield.hpp>

using byte_storage = metl::bitfield<0, 1, std::uint8_t>;
static_assert(byte_storage::width == 1, "control instantiation");

#ifdef METL_COMPILE_FAIL
using bool_storage = metl::bitfield<0, 1, bool>;
static_assert(bool_storage::width == 1, "forces the instantiation");
#endif
