#pragma once

#include "metl/compiler.hpp"

#include <type_traits>

namespace metl {
namespace detail {

// Whether converting the invoke result `U` to the return type `R` would bind
// `R` to a temporary: `R` is a reference and `U` is a prvalue that is the
// referenced value, or a reference to a type that only converts to `R`'s (an
// `int&` read as `const long&`). The thunk that performs that conversion returns
// a reference into its own frame. This is C++23's
// std::reference_converts_from_temporary, which GCC 13+ and newer Clang expose
// as a builtin in every language mode. Failing that, the direct-initialization
// form gives the same answer here, since is_invocable_r already requires the
// implicit conversion: __reference_constructs_from_temporary, or on older Clang
// __reference_binds_to_temporary. The newer name is tried first because Clang
// deprecates the older one in its favour, so a compiler that warns about
// __reference_binds_to_temporary never reaches it. Without any, it is spelled
// out:
//   - a reference to a type whose pointer converts to a pointer to `R`'s binds
//     directly (the same type, or a derived one);
//   - a class result that converts to a NON-const lvalue reference to `R`'s type
//     does so through a conversion function returning an lvalue, as
//     std::reference_wrapper does, and no temporary can bind a non-const lvalue
//     reference;
//   - anything else is assumed to need one. That refuses a class whose
//     conversion function returns `const T&`, which the builtins accept: the
//     fallback may refuse a safe callable, never accept a dangling one.
#if METL_HAS_BUILTIN(__reference_converts_from_temporary)
template <typename R, typename U>
struct binds_temporary : std::bool_constant<__reference_converts_from_temporary(R, U)> {};
#elif METL_HAS_BUILTIN(__reference_constructs_from_temporary)
template <typename R, typename U>
struct binds_temporary : std::bool_constant<__reference_constructs_from_temporary(R, U)> {};
#elif METL_HAS_BUILTIN(__reference_binds_to_temporary)
// Older Clang's __reference_binds_to_temporary, Clang 11's among them, answers
// false for a class prvalue: it counts binding the materialised object as
// direct. A prvalue of `R`'s referenced type, or of a class derived from it,
// always materialises one.
template <typename R, typename U>
struct binds_materialized_prvalue
    : std::conjunction<std::is_reference<R>,
                       std::negation<std::is_reference<U>>,
                       std::is_convertible<std::remove_reference_t<U>*, std::remove_reference_t<R>*>> {};
template <typename R, typename U>
struct binds_temporary : std::disjunction<binds_materialized_prvalue<R, U>,
                                          std::bool_constant<__reference_binds_to_temporary(R, U)>> {};
#else
// Each condition is its own trait so std::conjunction only instantiates it when
// the earlier ones hold: `R&` must not even be named for `R = void`.
template <typename R, typename U>
struct binds_directly
    : std::conjunction<std::is_reference<U>,
                       std::is_convertible<std::remove_reference_t<U>*, std::remove_reference_t<R>*>> {};
template <typename R, typename U>
struct converts_to_mutable_lvalue : std::is_convertible<U, std::remove_cv_t<std::remove_reference_t<R>>&> {};
template <typename R, typename U>
struct binds_temporary
    : std::conjunction<std::is_reference<R>,
                       std::negation<binds_directly<R, U>>,
                       std::negation<std::conjunction<std::is_lvalue_reference<R>,
                                                      std::is_class<std::remove_reference_t<U>>,
                                                      converts_to_mutable_lvalue<R, U>>>> {};
#endif

template <typename R, typename F, typename... Args>
struct invoke_binds_temporary : binds_temporary<R, std::invoke_result_t<F, Args...>> {};

/// `std::is_invocable_r_v`, refusing a result that `R` could only bind as a
/// temporary. `invoke_binds_temporary` is only instantiated for an invocable `F`.
template <typename R, typename F, typename... Args>
inline constexpr bool is_invocable_r_without_temporary_v =
    std::conjunction_v<std::is_invocable_r<R, F, Args...>,
                       std::negation<invoke_binds_temporary<R, F, Args...>>>;

/// The `noexcept` form, for the `R(Args...) noexcept` specialisations.
template <typename R, typename F, typename... Args>
inline constexpr bool is_nothrow_invocable_r_without_temporary_v =
    std::conjunction_v<std::is_nothrow_invocable_r<R, F, Args...>,
                       std::negation<invoke_binds_temporary<R, F, Args...>>>;

}  // namespace detail
}  // namespace metl
