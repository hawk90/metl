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

  return metl_test::exit_code();
}
