// EXPECT-ERROR: expected<void, E>::and_then(F): F must return a metl::expected<U, E>
//
// The void specialisation carries its own copy of the monadic constraints, so a
// fix to the general template does not reach it. #12 constrained the general
// one; this is the assertion that has to hold for `expected<void, E>`, and it
// is a separate line of code in a separate class.

#include <metl/expected.hpp>

namespace {
enum class io_error { timeout };
enum class parse_error { bad_digit };
}  // namespace

metl::expected<int, io_error> control(metl::expected<void, io_error> value) {
  return value.and_then([] { return metl::expected<int, io_error>(1); });
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::expected<void, io_error> value) {
  return value.and_then([] { return metl::expected<int, parse_error>(1); });
}
#endif
