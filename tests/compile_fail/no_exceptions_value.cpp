// EXPECT-ERROR: METL_NO_EXCEPTIONS must be 0 or 1
//
// The ABI tag spells the value into every METL symbol's namespace, so a value
// other than 0 or 1 would name a configuration that does not exist.

#ifdef METL_COMPILE_FAIL
#define METL_NO_EXCEPTIONS 2
#endif

#include <metl/config.hpp>

int main() {
  return 0;
}
