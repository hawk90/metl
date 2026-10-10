// A reference-returning signature must not accept a callable whose result has
// to be materialised as a temporary to bind that reference (#259). The thunk
// would return a reference to an object that dies inside it. libc++'s
// is_invocable_r accepted these in C++17/20 while libstdc++ rejected them, so
// the same code was undefined behaviour on one toolchain and an error on the
// other; C++23 settles it with reference_converts_from_temporary.
#include "metl_check.hpp"

#include <type_traits>

#include <metl/fixed_function.hpp>
#include <metl/function_ref.hpp>

namespace {

struct base {
  int value = 1;
};
struct derived : base {};

base g_base;
derived g_derived;
int g_int = 7;

struct by_value_t {
  base operator()() const { return base{}; }  // prvalue
};
struct by_reference_t {
  base& operator()() const { return g_base; }
};
struct by_derived_reference_t {
  derived& operator()() const { return g_derived; }
};
struct int_reference_t {
  int& operator()() const { return g_int; }  // int& -> const long& needs a temporary
};
struct by_rvalue_reference_t {
  base&& operator()() const { return static_cast<base&&>(g_base); }
};

template <typename Wrapper, typename Callable>
constexpr bool accepts = std::is_constructible_v<Wrapper, Callable&>;

// ---- rejected: the reference would bind a temporary --------------------------
static_assert(!accepts<metl::function_ref<const base&()>, by_value_t>, "function_ref: prvalue to const&");
static_assert(!accepts<metl::fixed_function<const base&()>, by_value_t>, "fixed_function: prvalue to const&");
static_assert(!accepts<metl::fixed_any_invocable<const base&()>, by_value_t>, "fixed_any_invocable: prvalue");
static_assert(!accepts<metl::function_ref<base && ()>, by_value_t>, "function_ref: prvalue to &&");
static_assert(!accepts<metl::function_ref<const long&()>, int_reference_t>, "int& to const long& converts");
static_assert(!accepts<metl::fixed_function<const long&()>, int_reference_t>, "int& to const long& converts");

// ---- accepted: the reference binds the callable's own result -----------------
static_assert(accepts<metl::function_ref<const base&()>, by_reference_t>, "lvalue result");
static_assert(accepts<metl::function_ref<const base&()>, by_derived_reference_t>, "derived to base");
static_assert(accepts<metl::fixed_function<base&()>, by_reference_t>, "lvalue result");
static_assert(accepts<metl::fixed_any_invocable<const base&()>, by_derived_reference_t>, "derived to base");
static_assert(accepts<metl::function_ref<base && ()>, by_rvalue_reference_t>, "xvalue result");
// A non-reference return type is unaffected: the callable's prvalue is the result.
static_assert(accepts<metl::function_ref<base()>, by_value_t>, "by value stays accepted");
static_assert(accepts<metl::fixed_function<base()>, by_value_t>, "by value stays accepted");
static_assert(accepts<metl::fixed_function<void()>, by_value_t>, "a discarded result stays accepted");

}  // namespace

int main() {
  by_derived_reference_t by_derived_reference;
  metl::function_ref<const base&()> ref = by_derived_reference;
  CHECK(&ref() == static_cast<const base*>(&g_derived));
  metl::fixed_function<base&()> stored = by_reference_t{};
  CHECK(&stored() == &g_base);
  metl::fixed_any_invocable<const base&()> owned = by_derived_reference_t{};
  CHECK(&owned() == static_cast<const base*>(&g_derived));
  return metl_test::exit_code();
}
