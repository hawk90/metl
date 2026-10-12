// EXPECT-ERROR: variant_alternative index out of range
//
// emplace<I> with I out of range. The return type, variant_alternative_t<I,
// variant>&, rejects it before the body is instantiated, so this is the message
// the caller sees. emplace<I> once carried its own range assertion in the body,
// which could never fire (#309).

#include <metl/variant.hpp>

void control() {
  metl::variant<int, long> v;
  v.emplace<1>(2L);
}

#ifdef METL_COMPILE_FAIL
void offender() {
  metl::variant<int, long> v;
  v.emplace<5>(2L);
}
#endif
