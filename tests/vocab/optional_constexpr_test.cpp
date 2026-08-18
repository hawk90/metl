// Proves metl::optional is GENUINELY constexpr on a C++20 toolchain (its
// storage lifetime runs through metl::detail::construct_at / destroy_at, which
// are constant-evaluable in C++20) and is a harmless no-op on C++17, where the
// placement-new fallback is — correctly — not constant-evaluable.
//
// The static_asserts below are compiled only where that path is actually active;
// elsewhere the file still builds and its runtime smoke still runs, so the test
// is green in every configuration in the matrix.
//
// Which means it is green when it verified nothing, so it says which. Until the
// C++20 CI leg existed, nothing ever compiled the asserts at all.

#include "metl_check.hpp"

#include <cstdio>

#include <metl/detail/construct.hpp>
#include <metl/optional.hpp>

// Gate on the macro the LIBRARY gates on, not on __cplusplus.
//
// The two are not the same condition, and assuming they were is what broke this
// test the first time it was ever compiled. detail/construct.hpp turns the
// constexpr-lifetime path on only when the standard LIBRARY also provides a
// constexpr std::construct_at (__cpp_lib_constexpr_dynamic_alloc). A toolchain
// can be in C++20 mode with a standard library that does not, and there
// METL_CONSTEXPR20 is empty, optional's destructor is not constexpr, optional is
// therefore not a literal type -- and static_asserts written against __cplusplus
// demand constexpr from a type the library never claimed was constexpr.
#if METL_DETAIL_CONSTEXPR_LIFETIME

// A value constructed and observed entirely within constant evaluation.
constexpr int constexpr_roundtrip() {
  const metl::optional<int> some{42};
  const metl::optional<int> none{};
  const int a = some.has_value() ? some.value() : -1;
  const int b = none.has_value() ? 100 : 0;
  return a + b;  // 42
}
static_assert(constexpr_roundtrip() == 42, "optional must be constant-evaluable in C++20");

// A constexpr variable with a non-trivial (but constexpr) destructor — requires
// constant destruction, which only works with the C++20 lifetime path.
constexpr metl::optional<int> kGlobal{7};
static_assert(kGlobal.has_value(), "constexpr optional variable must hold a value");
static_assert(*kGlobal == 7, "constexpr optional operator* must be usable in constant evaluation");

// in_place construction is likewise constant-evaluable.
constexpr metl::optional<int> kInPlace{metl::in_place, 9};
static_assert(kInPlace.value() == 9, "constexpr in_place optional");

#endif  // METL_DETAIL_CONSTEXPR_LIFETIME

int main() {
#if METL_DETAIL_CONSTEXPR_LIFETIME
  std::printf("optional_constexpr_test: constexpr-lifetime path ACTIVE (asserts compiled)\n");
#else
  std::printf("optional_constexpr_test: constexpr-lifetime path inactive; runtime smoke only\n");
#endif

  metl::optional<int> o{5};
  CHECK(o.has_value());
  CHECK_EQ(*o, 5);
  o.reset();
  CHECK(!o.has_value());
  metl::optional<int> e{metl::in_place, 11};
  CHECK_EQ(e.value(), 11);
  return metl_test::exit_code();
}
