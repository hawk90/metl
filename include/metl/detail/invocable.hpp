#pragma once

#include <type_traits>

namespace metl {
namespace detail {

// Whether converting the invoke result `U` to the return type `R` would bind
// `R` to a temporary: `R` is a reference and `U` is a prvalue, or a reference
// to a type that only converts to `R`'s (an `int&` read as `const long&`).
// The thunk that performs that conversion returns a reference into its own
// frame. C++23's std::reference_converts_from_temporary names this; C++17 has
// no trait, so it is spelled out here: a reference binds without a temporary
// exactly when a pointer to the referenced type converts to a pointer to `R`'s.
template <typename R, typename U>
struct binds_temporary
    : std::bool_constant<std::is_reference_v<R> &&
                         (!std::is_reference_v<U> ||
                          !std::is_convertible_v<std::remove_reference_t<U>*, std::remove_reference_t<R>*>)> {
};

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
