// Regression tests for guarded, from the 2026-08-21 audit. Kept apart from
// sync/audit_regressions_test.cpp, whose mpmc_queue cases need a lock-free CAS
// and so cannot build on ARMv6-M; guarded needs none, and runs there.

#include "metl_check.hpp"

#include "metl/lock.hpp"

#include <type_traits>

int main() {
  // ---------------------------------------------------------------------
  // guarded::with must not let a reference to the guarded value escape.
  // ---------------------------------------------------------------------
  {
    using guard_type = metl::guarded<int, metl::null_lock>;
    guard_type shared;

    // Returning a value is fine, and the lambda sees the guarded object.
    const int doubled = shared.with([](int& v) {
      v = 21;
      return v * 2;
    });
    CHECK_EQ(doubled, 42);

    // Returning void is fine.
    shared.with([](int& v) noexcept { v += 1; });
    CHECK_EQ(shared.with([](int& v) { return v; }), 22);

    // Returning a reference to something that is NOT the guarded value is still
    // allowed -- the check rejects the escape it can see, not every one.
    static int elsewhere = 5;
    int& other = shared.with([](int&) -> int& { return elsewhere; });
    CHECK_EQ(other, 5);

    // Returning a reference to the GUARDED value is a documented hazard, not a
    // compile error, and the line above is why: for guarded<int> a returned
    // `int&` to an unrelated global and one to the guarded value are the same
    // type. A static_assert on the return type was tried and rejected the line
    // above, which is correct code. See the @warning on guarded::with.
  }

  // ---------------------------------------------------------------------
  // the variadic constructor must not out-compete the deleted copy ctor.
  // `guarded` must still be non-copyable, and the error must say so.
  // ---------------------------------------------------------------------
  {
    static_assert(!std::is_copy_constructible_v<metl::guarded<int, metl::null_lock>>,
                  "guarded must not be copy-constructible");
    static_assert(!std::is_move_constructible_v<metl::guarded<int, metl::null_lock>>,
                  "guarded must not be move-constructible");
    // In-place construction still works, which is what the variadic is for.
    metl::guarded<int, metl::null_lock> from_value{7};
    CHECK_EQ(from_value.with([](int& v) { return v; }), 7);
  }

  return metl_test::exit_code();
}
