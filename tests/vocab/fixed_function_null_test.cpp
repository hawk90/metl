// Regression test: a null function pointer whose signature only CONVERTS to
// the target's must be refused like an exact-signature one.
//
// `int (*)(long)` into `fixed_function<int(int)>` takes the generic callable
// path, not the exact-signature overload that asserts non-null, so it used to
// be stored as an engaged target -- `has_value()` true, and calling it jumped
// to address 0. The assert is observed with a handler that longjmps out before
// the abort, as tests/core/hardening_common.h does, so this runs on QEMU too.

#include "metl_check.hpp"

#include <csetjmp>

#include <metl/assert.hpp>
#include <metl/fixed_function.hpp>

namespace {

std::jmp_buf g_jump;
bool g_asserted = false;

void capture(const char* /*expr*/, const char* /*file*/, int /*line*/) noexcept {
  g_asserted = true;
  std::longjmp(g_jump, 1);
}

int widen(long v) {
  return static_cast<int>(v) + 1;
}

int widen_int(int v) noexcept {
  return v + 1;
}

}  // namespace

int main() {
  metl::set_assert_handler(&capture);

  // Converting signature, null: METL_HARDEN, so this holds at every level.
  {
    int (*null_fn)(long) = nullptr;
    g_asserted = false;
    if (setjmp(g_jump) == 0) {
      metl::fixed_function<int(int)> f(null_fn);
      (void)f;
    }
    CHECK(g_asserted);
  }
  {
    int (*null_fn)(long) = nullptr;
    g_asserted = false;
    if (setjmp(g_jump) == 0) {
      metl::fixed_any_invocable<int(int)> f(null_fn);
      (void)f;
    }
    CHECK(g_asserted);
  }

  // Converting signature, non-null: stored and callable, no assert.
  {
    g_asserted = false;
    int (*fn)(long) = &widen;
    metl::fixed_function<int(int)> f(fn);
    CHECK(!g_asserted);
    CHECK(f.has_value());
    CHECK_EQ(f(41), 42);
  }

  // An empty wrapper of another capacity, signature or kind converts to an
  // empty one. It used to be wrapped as a callable: `if (f)` held, and the
  // call asserted inside the empty inner wrapper.
  {
    const metl::fixed_function<int(int), 16> empty_small;
    const metl::fixed_function<int(int) noexcept, 16> empty_noexcept;

    metl::fixed_function<int(int), 64> copied(empty_small);
    CHECK(!copied);
    CHECK(copied == nullptr);
    metl::fixed_function<int(int), 64> moved(metl::fixed_function<int(int), 16>{});
    CHECK(!moved);
    metl::fixed_function<int(int), 64> from_noexcept(empty_noexcept);
    CHECK(!from_noexcept);
    metl::fixed_any_invocable<int(int), 64> any(empty_small);
    CHECK(!any);
    CHECK(any == nullptr);
    metl::fixed_any_invocable<int(int), 64> any_moved(metl::fixed_any_invocable<int(int), 16>{});
    CHECK(!any_moved);

    // Assignment replaces an engaged target with nothing, and try_assign
    // reports success: there was nothing to store.
    metl::fixed_function<int(int), 64> assigned(&widen_int);
    CHECK(assigned.has_value());
    assigned = empty_small;
    CHECK(!assigned);
    metl::fixed_any_invocable<int(int), 64> any_assigned(&widen_int);
    CHECK(any_assigned.try_assign(metl::fixed_any_invocable<int(int), 16>{}));
    CHECK(!any_assigned);
    metl::fixed_function<int(int), 64> try_assigned(&widen_int);
    CHECK(try_assigned.try_assign(empty_small));
    CHECK(!try_assigned);

    // A source too large to store still converts when it is empty.
    const metl::fixed_function<int(int), 256> empty_large;
    metl::fixed_function<int(int), 64> from_large;
    CHECK(from_large.try_assign(empty_large));
    CHECK(!from_large);
  }

  // An engaged one still converts to a wrapper that calls the original target.
  {
    const metl::fixed_function<int(int), 16> small(&widen_int);
    metl::fixed_function<int(int), 64> big(small);
    CHECK(big.has_value());
    CHECK_EQ(big(41), 42);
    metl::fixed_any_invocable<int(int), 64> any(small);
    CHECK(any.has_value());
    CHECK_EQ(any(1), 2);
  }

  return metl_test::exit_code();
}
