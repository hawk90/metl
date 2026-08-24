// EXPECT-ERROR: expected<void, E>::or_else(F): F must return a metl::expected<void, G>
//
// The last of the four monadic constraints, and the one furthest from the
// tests: `expected<void, E>::or_else` must keep the void value type while the
// error type is free to change. A callable returning a value-carrying expected
// would turn a "did it work" into a "what did it produce".

#include <metl/expected.hpp>

namespace {
enum class io_error { timeout };
enum class other_error { closed };
}  // namespace

metl::expected<void, other_error> control(metl::expected<void, io_error> value) {
  return value.or_else([](io_error) { return metl::expected<void, other_error>(); });
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::expected<void, io_error> value) {
  return value.or_else([](io_error) { return metl::expected<int, other_error>(0); });
}
#endif
