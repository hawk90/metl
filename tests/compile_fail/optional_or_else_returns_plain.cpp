// EXPECT-ERROR: optional::or_else(F): F must return a metl::optional
//
// `or_else` supplies a REPLACEMENT optional when this one is empty, so its
// callable must return the same kind of thing it is replacing. A callable
// returning a bare value reads like a default -- that is `value_or` -- and
// accepting it would make the two spellings mean different things at different
// call sites.

#include <metl/optional.hpp>

metl::optional<int> control(metl::optional<int> value) {
  return value.or_else([] { return metl::optional<int>(0); });
}

#ifdef METL_COMPILE_FAIL
auto offender(metl::optional<int> value) {
  return value.or_else([] { return 0; });
}
#endif
