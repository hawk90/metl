// EXPECT-ERROR: METL_HARDENING must be 0 (NONE), 1 (FAST), or 2 (DEBUG)
//
// The level's name instead of its macro. The preprocessor reads an unknown
// identifier as 0, so a range check took -DMETL_HARDENING=DEBUG as NONE and
// compiled every precondition check out without a word.

#ifdef METL_COMPILE_FAIL
#define METL_HARDENING DEBUG
#else
#define METL_HARDENING METL_HARDENING_DEBUG
#endif

#include <metl/config.hpp>

int main() {
  return 0;
}
