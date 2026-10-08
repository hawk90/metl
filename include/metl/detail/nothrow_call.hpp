#pragma once

/// @file
/// @brief Whether calling a comparator, equality or hash object can throw -- the
///        input to the conditional `noexcept` of the containers' lookups.
///
/// The containers' lookups (`find`, `contains`, `lower_bound`, `erase`, ...) call
/// user code: the comparator, the hasher, the key equality. They are `noexcept`
/// exactly when that code cannot throw; an unconditional `noexcept` would turn
/// a throwing comparator into a call to `std::terminate`.
///
/// The answer cannot simply be `noexcept(comp(a, b))`: `std::less`,
/// `std::greater` and `std::equal_to` do not declare their call operators
/// `noexcept`, so the default comparator would strip `noexcept` from every
/// lookup -- even on `int`, even on a `metl::fixed_string` whose `<` is
/// `noexcept`. For those three (typed or transparent), look through to the
/// operator they apply. Every other callable is taken at its word.
///
/// Every header that uses this already includes `<functional>` for its default
/// comparator, so it adds no dependency.

#include <functional>
#include <type_traits>
#include <utility>

namespace metl {
namespace detail {

/// @brief `true` when `F{}(a, b)` with `a : A`, `b : B` cannot throw.
template <typename F, typename A, typename B>
struct nothrow_binary_call
    : std::bool_constant<noexcept(std::declval<F&>()(std::declval<A>(), std::declval<B>()))> {};

template <typename T, typename A, typename B>
struct nothrow_binary_call<std::less<T>, A, B>
    : std::bool_constant<noexcept(std::declval<A>() < std::declval<B>())> {};

template <typename T, typename A, typename B>
struct nothrow_binary_call<std::greater<T>, A, B>
    : std::bool_constant<noexcept(std::declval<A>() > std::declval<B>())> {};

template <typename T, typename A, typename B>
struct nothrow_binary_call<std::equal_to<T>, A, B>
    : std::bool_constant<noexcept(std::declval<A>() == std::declval<B>())> {};

/// @brief Shorthand for `nothrow_binary_call<F, A, B>::value`.
template <typename F, typename A, typename B>
inline constexpr bool nothrow_binary_call_v = nothrow_binary_call<F, A, B>::value;

/// @brief `true` when `F{}(a)` with `a : A` cannot throw (a hasher).
template <typename F, typename A>
inline constexpr bool nothrow_unary_call_v = noexcept(std::declval<F&>()(std::declval<A>()));

}  // namespace detail
}  // namespace metl
