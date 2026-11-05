// EXPECT-ERROR: METL_ASSERT must not be predefined
//
// Predefined to a no-op, an over-capacity push_back on fixed_vector<int, 1>
// was dropped without a word (measured in the 2.0 re-review). The level is
// METL_HARDENING's to choose.

#ifdef METL_COMPILE_FAIL
#define METL_ASSERT(expr) static_cast<void>(0)
#endif

#include <metl/config.hpp>

int main() {
  return 0;
}
