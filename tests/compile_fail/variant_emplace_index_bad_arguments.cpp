// EXPECT-ERROR: alternative must be constructible from the given arguments
//
// The index-addressed twin of the emplace<T> constructibility check. Reachable
// where `emplace<I> index out of range` is not: with I in range the return type
// `variant_alternative_t<I, variant>&` instantiates cleanly, so control reaches
// the body and this assertion is the one that fires.

#include <metl/variant.hpp>

namespace {

struct needs_two {
  needs_two(int, int) {}
};

}  // namespace

using holder = metl::variant<int, needs_two>;

void control(holder& value) {
  value.emplace<1>(1, 2);
}

#ifdef METL_COMPILE_FAIL
void offender(holder& value) {
  value.emplace<1>(1);
}
#endif
