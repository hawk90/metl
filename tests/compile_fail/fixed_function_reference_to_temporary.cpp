// EXPECT-ERROR: fixed_function returns a reference, and the callable's result would bind it to a temporary
//
// A callable that returns by value, stored behind a signature that returns a
// reference, would hand back a reference to a temporary that dies inside the
// call (#259). The constructors refuse it through their constraints; this pins
// the try_assign path, which reports it by name.

#include <metl/fixed_function.hpp>

struct big {
  long words[8];
};

inline const big& control() {
  static big kept{};
  metl::fixed_function<const big&()> f([]() -> const big& { return kept; });
  return f();
}

#ifdef METL_COMPILE_FAIL
inline bool stores_a_by_value_callable() {
  metl::fixed_function<const big&()> f;
  return f.try_assign([] { return big{}; });
}
#endif
