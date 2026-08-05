// Shared body for the METL_HARDENING level tests. Each thin TU
// (hardening_{none,fast,debug}_test.cpp) predefines METL_HARDENING to a level
// and then includes this file, which verifies exactly which of the three assert
// tiers fire at that level:
//
//   METL_ASSERT   — fires at FAST and DEBUG   (>= METL_HARDENING_FAST)
//   METL_DASSERT  — fires at DEBUG only        (>= METL_HARDENING_DEBUG)
//   METL_HARDEN   — always fires               (never stripped)
//
// Firing is observed WITHOUT any real UB: a custom handler longjmps back out
// before the guaranteed [[noreturn]] abort (the same trick as assert_test), so
// this is deterministic and sanitizer-safe at every level.
#pragma once

#include <csetjmp>

#include <metl/assert.hpp>
#include <metl/config.hpp>

namespace {

std::jmp_buf g_jmp;
bool g_called = false;

void capture_handler(const char* /*expr*/, const char* /*file*/, int /*line*/) noexcept {
  g_called = true;
  std::longjmp(g_jmp, 1);
}

}  // namespace

int main() {
  metl::set_assert_handler(&capture_handler);

  g_called = false;
  if (setjmp(g_jmp) == 0) {
    METL_ASSERT(false);
  }
  const bool assert_fired = g_called;

  g_called = false;
  if (setjmp(g_jmp) == 0) {
    METL_DASSERT(false);
  }
  const bool dassert_fired = g_called;

  g_called = false;
  if (setjmp(g_jmp) == 0) {
    METL_HARDEN(false);
  }
  const bool harden_fired = g_called;

  const bool expect_assert = (METL_HARDENING >= METL_HARDENING_FAST);
  const bool expect_dassert = (METL_HARDENING >= METL_HARDENING_DEBUG);
  const bool expect_harden = true;

  if (assert_fired != expect_assert) {
    return 1;
  }
  if (dassert_fired != expect_dassert) {
    return 2;
  }
  if (harden_fired != expect_harden) {
    return 3;
  }
  return 0;
}
