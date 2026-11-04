// EXPECT-WARNING: static_allocator is removed in 2.0
//
// static_allocator is deprecated for 2.0 (#645): a use warns, naming the replacement,
// and the replacement builds without a warning.

#include <metl/monotonic_buffer.hpp>
#include <metl/static_allocator.hpp>

int* control(metl::monotonic_buffer<64>& b) {
  return b.try_allocate_n<int>(2);
}

#ifdef METL_COMPILE_FAIL
int* offender(metl::static_allocator<int, 64>& a) {
  return a.try_allocate(2);
}
#endif
