// EXPECT-ERROR: METL_HARDEN must not be predefined
//
// Predefined to a no-op, it removed the always-on security floor that
// METL_HARDENING_NONE itself keeps.

#ifdef METL_COMPILE_FAIL
#define METL_HARDEN(expr) static_cast<void>(0)
#endif

#include <metl/config.hpp>

int main() {
  return 0;
}
