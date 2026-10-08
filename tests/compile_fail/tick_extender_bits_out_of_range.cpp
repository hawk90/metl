// EXPECT-ERROR: tick_extender Bits must be 1..63
//
// The extended tick is 64 bits. A 64-bit "counter" has nothing to extend, and
// zero bits has no period: (raw - total) & mask would be 0 for every reading
// and the clock would stand still without a word.

#include <metl/tick_extender.hpp>

using thirty_two = metl::tick_extender<32, metl::null_lock>;
static_assert(sizeof(thirty_two) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using sixty_four = metl::tick_extender<64, metl::null_lock>;
static_assert(sizeof(sixty_four) > 0, "forces the instantiation");
#endif
