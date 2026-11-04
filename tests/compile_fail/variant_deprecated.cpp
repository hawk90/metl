// EXPECT-WARNING: metl::variant is removed in 2.0
//
// metl::variant is deprecated for 2.0 (#635): a use warns, naming std::variant,
// and including the header alone does not.

#include <variant>

#include <metl/variant.hpp>

int control(const std::variant<int, char>& v) {
  return static_cast<int>(v.index());
}

#ifdef METL_COMPILE_FAIL
int offender(const metl::variant<int, char>& v) {
  return static_cast<int>(v.index());
}
#endif
