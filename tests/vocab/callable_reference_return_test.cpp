// A reference-returning signature must not accept a callable whose result has
// to be materialised as a temporary to bind that reference (#259). The thunk
// would return a reference to an object that dies inside it. libc++'s
// is_invocable_r accepted these in C++17/20 while libstdc++ rejected them, so
// the same code was undefined behaviour on one toolchain and an error on the
// other; C++23 settles it with reference_converts_from_temporary.
#include "metl_check.hpp"

#include <functional>
#include <type_traits>

#include <metl/compiler.hpp>
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

// A class result whose conversion function returns an lvalue reference binds
// the returned reference to that lvalue: no temporary (#284).
struct int_handle {
  operator int&() const { return g_int; }
};
struct const_int_handle {
  operator const int&() const { return g_int; }
};
struct by_reference_wrapper_t {
  std::reference_wrapper<int> operator()() const { return std::ref(g_int); }
};
struct by_handle_t {
  int_handle operator()() const { return {}; }
};
struct by_const_handle_t {
  const_int_handle operator()() const { return {}; }
};
struct by_value_handle_t {
  struct to_long {
    operator long() const { return 1; }
  };
  to_long operator()() const { return {}; }
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
static_assert(accepts<metl::function_ref<int&()>, by_reference_wrapper_t>, "reference_wrapper to T&");
static_assert(accepts<metl::fixed_function<int&()>, by_reference_wrapper_t>, "reference_wrapper to T&");
static_assert(accepts<metl::fixed_any_invocable<const int&()>, by_reference_wrapper_t>, "reference_wrapper");
static_assert(accepts<metl::function_ref<int&()>, by_handle_t>, "operator int& to int&");
static_assert(accepts<metl::fixed_function<const int&()>, by_handle_t>, "operator int& to const int&");
#if METL_HAS_BUILTIN(__reference_converts_from_temporary) || METL_HAS_BUILTIN(__reference_binds_to_temporary)
// Only the compiler can see that `operator const int&` yields an lvalue; the
// portable fallback cannot tell it from `operator int`, and refuses both.
static_assert(accepts<metl::function_ref<const int&()>, by_const_handle_t>, "operator const int&");
#endif
// A conversion that yields a value still needs a temporary.
static_assert(!accepts<metl::function_ref<const long&()>, by_value_handle_t>, "operator long to const long&");
static_assert(!accepts<metl::fixed_function<const long&()>, by_reference_wrapper_t>, "int& to const long&");

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
  by_reference_wrapper_t wrapper;
  metl::function_ref<int&()> wrapped = wrapper;
  CHECK(&wrapped() == &g_int);
  metl::fixed_function<const int&()> handled = by_handle_t{};
  CHECK(&handled() == &g_int);
  return metl_test::exit_code();
}
