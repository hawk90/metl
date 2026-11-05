// EXPECT-ERROR: METL_DASSERT must not be predefined
//
// Predefined, it took the debug checks out of METL_HARDENING's hands.

#ifdef METL_COMPILE_FAIL
#define METL_DASSERT(expr) static_cast<void>(0)
#endif

#include <metl/config.hpp>

int main() {
  return 0;
}
