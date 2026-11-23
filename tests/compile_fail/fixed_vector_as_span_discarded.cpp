// EXPECT-WARNING: nodiscard
//
// as_span() only returns a view, so calling it as a statement does nothing
// (#545). It is METL_NODISCARD, as on fixed_string and fixed_priority_queue.

#include <metl/fixed_vector.hpp>

int control(const metl::fixed_vector<int, 4>& v) {
  return static_cast<int>(v.as_span().size());
}

#ifdef METL_COMPILE_FAIL
void offender(const metl::fixed_vector<int, 4>& v) {
  v.as_span();
}
#endif
