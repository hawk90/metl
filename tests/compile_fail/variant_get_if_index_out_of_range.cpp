// EXPECT-ERROR: variant_alternative index out of range
//
// get_if<I> with I out of range. The return type rejects it before the body is
// instantiated, so this is the message the caller sees. get_if<I> once carried
// its own range assertion in the body, which could never fire (#309).

#include <metl/variant.hpp>

void control() {
  metl::variant<int, long> v;
  (void)metl::get_if<1>(&v);
}

#ifdef METL_COMPILE_FAIL
void offender() {
  metl::variant<int, long> v;
  (void)metl::get_if<5>(&v);
}
#endif
