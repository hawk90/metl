// EXPECT-ERROR: METL_CACHELINE_ALIGNED must not be predefined
//
// Predefined, it changed the alignment of the three types' indices and
// storage without changing METL_CACHELINE_SIZE, the same ODR violation.

#ifdef METL_COMPILE_FAIL
#define METL_CACHELINE_ALIGNED alignas(4)
#endif

#include <metl/optimization.hpp>

int main() {
  return 0;
}
