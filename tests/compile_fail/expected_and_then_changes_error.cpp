// EXPECT-ERROR: expected::and_then(F): F must return a metl::expected with the same error type E
//
// `and_then` propagates the existing error unchanged, so the callable's
// expected must carry the SAME E. A callable that returns a different error
// type has no meaning here: there is nowhere for the original error to go, and
// without the assertion the conversion that made it compile would be silent.
//
// The same shape as the `metl::visit` result-type defect (#84): a template that
// deduces from one side and quietly converts the other.

#include <metl/expected.hpp>

namespace {

enum class io_error { timeout, closed };
enum class parse_error { bad_digit };

}  // namespace

metl::expected<long, io_error> control(metl::expected<int, io_error> value) {
  return value.and_then([](int held) { return metl::expected<long, io_error>(static_cast<long>(held)); });
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::expected<int, io_error> value) {
  return value.and_then([](int held) { return metl::expected<long, parse_error>(static_cast<long>(held)); });
}
#endif
