// EXPECT-ERROR: fixed_function requires a callable that is nothrow move constructible
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
  metl::fixed_function<int(int)> f(lambda);
  return f(1);
}

#ifdef METL_COMPILE_FAIL
// The converting constructor is constrained on the same condition, so the
// message is reached through try_assign; tests/vocab/fixed_function_test.cpp
// checks that the constructor is not viable.
bool offender() {
  metl::fixed_function<int(int)> f;
  return f.try_assign(throwing_move_callable{});
}
#endif
