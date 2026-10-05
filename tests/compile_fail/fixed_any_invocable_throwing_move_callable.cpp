// EXPECT-ERROR: fixed_any_invocable requires a callable that is nothrow move constructible
//
// The wrapper's move, move-assignment and swap are noexcept and relocate the
// stored callable through a type-erased move. A callable whose move can throw
// used to be accepted, and moving the wrapper then terminated the program.
// The wrapper cannot make its own noexcept depend on what
// it holds, so the callable is refused when it is stored.

#include <metl/fixed_function.hpp>

namespace {

struct throwing_move_callable {
  throwing_move_callable() = default;
  throwing_move_callable(const throwing_move_callable&) = default;
  throwing_move_callable(throwing_move_callable&&) noexcept(false) {}
  int operator()(int x) const { return x; }
};

}  // namespace

int control() {
  auto lambda = [](int x) { return x + 1; };  // moves without throwing
  metl::fixed_any_invocable<int(int)> f(lambda);
  return f(1);
}

#ifdef METL_COMPILE_FAIL
int offender() {
  metl::fixed_any_invocable<int(int)> f(throwing_move_callable{});
  return f(1);
}
#endif
