// Regression test: fixed_function::operator() is const but invokes a target
// that may mutate its own state (mirroring std::function / mutable lambdas).
// The stored callable now lives in a `mutable` buffer, so mutating it through a
// *const* fixed_function is well-defined rather than const-object UB. Run under
// UBSan this exercises the previously-undefined const_cast-and-mutate path.

#include "metl_check.hpp"

#include <metl/fixed_function.hpp>

int main() {
  // A stateful mutable lambda: each call mutates its own captured counter.
  auto counter = [n = 0]() mutable { return ++n; };

  const metl::fixed_function<int()> f = counter;  // NOTE: const instance
  CHECK_EQ(f(), 1);
  CHECK_EQ(f(), 2);
  CHECK_EQ(f(), 3);

  // noexcept signature variant, also const.
  const metl::fixed_function<int() noexcept> g = [n = 10]() mutable noexcept { return n++; };
  CHECK_EQ(g(), 10);
  CHECK_EQ(g(), 11);

  return metl_test::exit_code();
}
