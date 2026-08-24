// EXPECT-ERROR: expected::or_else(F): F must return a metl::expected with the same value type T
//
// The mirror of and_then: or_else replaces the ERROR side, so the value type
// has to survive untouched. A callable returning a different T would make the
// two branches of the same expression produce different types, and the one
// that compiled would be whichever the deduction happened to pick.

#include <metl/expected.hpp>

namespace {
enum class io_error { timeout };
enum class other_error { closed };
}  // namespace

metl::expected<int, other_error> control(metl::expected<int, io_error> value) {
  return value.or_else([](io_error) { return metl::expected<int, other_error>(0); });
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::expected<int, io_error> value) {
  return value.or_else([](io_error) { return metl::expected<long, other_error>(0L); });
}
#endif
