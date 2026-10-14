#pragma once

// Trips a METL_ASSERT and reports that it fired (#364).
//
//   CHECK(metl_test::trips([&] { (void)v[5]; }));
//
// runs the callable with an assert handler that longjmps back out before the
// guaranteed abort, as tests/core/hardening_common.h does, and returns whether
// an assert fired. Each firing also prints one line,
//
//   metl-assert-trip <file>:<line>
//
// which tools/check_assert_trips.py reads from ctest's output and compares
// with every METL_ASSERT in include/metl.
//
// The longjmp leaves the callable's frames without running their destructors,
// so the callable must not own anything that needs one: trip on objects that
// live outside it. METL_ASSERT is stripped below METL_HARDENING_FAST, where
// nothing can fire; check metl_test::asserts_active before relying on a trip.

#include "metl_check.hpp"

#include <csetjmp>
#include <cstdio>

#include <metl/assert.hpp>

namespace metl_test {

namespace detail {

inline std::jmp_buf& trip_jump() {
  static std::jmp_buf jump;
  return jump;
}

inline bool& trip_fired() {
  static bool fired = false;
  return fired;
}

inline void record_trip(const char* file, int line) noexcept {
  (void)std::printf("metl-assert-trip %s:%d\n", file, line);
  (void)std::fflush(stdout);  // the longjmp that follows must not lose it
}

inline void trip_handler(const char* /*expr*/, const char* file, int line) noexcept {
  trip_fired() = true;
  record_trip(file, line);
  std::longjmp(trip_jump(), 1);
}

}  // namespace detail

// The handler that was installed before a trip. Static, not a local: a value
// live across setjmp may be clobbered by the longjmp, and GCC reports the one
// inside the inlined set_assert_handler (-Wclobbered), as in assert_test.cpp.
inline metl::assert_handler_t& previous_handler() {
  static metl::assert_handler_t handler = nullptr;
  return handler;
}

template <typename F>
bool trips(F&& f) {
  previous_handler() = metl::set_assert_handler(&detail::trip_handler);
  detail::trip_fired() = false;
  if (setjmp(detail::trip_jump()) == 0) {
    f();
  }
  (void)metl::set_assert_handler(previous_handler());
  return detail::trip_fired();
}

}  // namespace metl_test
