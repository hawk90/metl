// EXPECT-ERROR: metl::ring_buffer::push_overwrite needs Capacity > 0
//
// A zero-capacity buffer is always full, so push_overwrite popped an empty
// buffer and aborted at every hardening level, although it is documented never
// to assert on a full buffer. It has no slot to overwrite and no element whose
// reference it could return.

#include <metl/ring_buffer.hpp>

int main() {
  metl::ring_buffer<int, 1> one;
  (void)one.push_overwrite(1);
  metl::ring_buffer<int, 0> none;  // the rest of a zero-capacity buffer still compiles
  (void)none.try_push_back(1);
#ifdef METL_COMPILE_FAIL
  (void)none.push_overwrite(1);
#endif
}
