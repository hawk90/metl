// EXPECT-WARNING: allocate asserts on failure in 2.0
//
// monotonic_buffer::allocate is deprecated for 2.0 (#645): a use warns, naming the replacement,
// and the replacement builds without a warning.

#include <metl/monotonic_buffer.hpp>

void* control(metl::monotonic_buffer<64>& b) {
  return b.try_allocate(8);
}

#ifdef METL_COMPILE_FAIL
void* offender(metl::monotonic_buffer<64>& b) {
  return b.allocate(8);
}
#endif
