// EXPECT-ERROR: get_if<T> requires unique alternative type
//
// get<T> is only meaningful when exactly one alternative has that type. Asking
// for a type the variant does not hold is the common typo; asking for one it
// holds twice is the subtle one. Both land on this assertion.

#include <metl/variant.hpp>

void control(metl::variant<int, long>& value) {
  (void)metl::get<int>(value);
}

#ifdef METL_COMPILE_FAIL
void offender(metl::variant<int, long>& value) {
  (void)metl::get<char>(value);
}
#endif
