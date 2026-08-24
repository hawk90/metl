// EXPECT-ERROR: holds_alternative<T> requires unique alternative type
//
// With the same type listed twice, "does it hold a T" has no single answer --
// it holds one of two different alternatives that happen to share a type. A
// permissive implementation would answer for whichever it found first, and the
// caller's next get<T> would read the other one.

#include <metl/variant.hpp>

bool control(const metl::variant<int, long>& value) {
  return metl::holds_alternative<int>(value);
}

#ifdef METL_COMPILE_FAIL
bool offender(const metl::variant<int, int>& value) {
  return metl::holds_alternative<int>(value);
}
#endif
