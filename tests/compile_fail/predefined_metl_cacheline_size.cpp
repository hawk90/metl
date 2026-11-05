// EXPECT-ERROR: METL_CACHELINE_SIZE must not be predefined
//
// Predefined, it gave two translation units different layouts of spsc_queue,
// spsc_byte_ring and mpmc_queue: an ODR violation no tool reported.

#ifdef METL_COMPILE_FAIL
#define METL_CACHELINE_SIZE 32
#endif

#include <metl/optimization.hpp>

int main() {
  return 0;
}
