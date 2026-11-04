// EXPECT-WARNING: allocate asserts on failure in 2.0
//
// arena_allocator::allocate is deprecated for 2.0 (#645): a use warns, naming the replacement,
// and the replacement builds without a warning.

#include <metl/arena_allocator.hpp>

void* control(metl::arena_allocator<64>& a) {
  return a.try_allocate(8);
}

#ifdef METL_COMPILE_FAIL
void* offender(metl::arena_allocator<64>& a) {
  return a.allocate(8);
}
#endif
