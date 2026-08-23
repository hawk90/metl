// EXPECT-ERROR: metl::visit requires the visitor to return the SAME type
//
// #84's case, and the reason this directory exists.
//
// A visitor whose result type differs per alternative used to compile and
// silently truncate: metl::visit([](auto x) { return x; }, v) on a
// variant<int32_t, int64_t> holding 5,000,000,000 returned 705032704, because
// the result type is deduced from alternative ZERO and every other alternative
// was converted to it. std::visit rejects the same code outright.
//
// #84 turned it into a static_assert and could only test the TRAIT, not the
// call -- instantiating visit with a bad visitor is now a hard error and there
// was nowhere to put a case that expects one. This is that place.

#include <cstdint>

#include <metl/variant.hpp>

std::int64_t control(metl::variant<std::int32_t, std::int64_t>& value) {
  return metl::visit([](auto input) -> std::int64_t { return input; }, value);
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::variant<std::int32_t, std::int64_t>& value) {
  return metl::visit([](auto input) { return input; }, value);
}
#endif
