// EXPECT-ERROR: optional::and_then(F): F must return a metl::optional
//
// `and_then` chains a computation that may itself fail, so its callable must
// return an optional. A callable returning a plain value is the natural mistake
// -- that is `transform` -- and without this assertion the chain would compile
// and the "empty" case would be a value nobody produced.
//
// #12 added this constraint. Nothing pinned it: a unit test can only exercise
// the callables the constraint ACCEPTS, so a constraint gone always-true stays
// green in every test that exists.

#include <metl/optional.hpp>

metl::optional<int> control(metl::optional<int> value) {
  return value.and_then([](int held) { return metl::optional<int>(held + 1); });
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::optional<int> value) {
  return value.and_then([](int held) { return held + 1; });
}
#endif
