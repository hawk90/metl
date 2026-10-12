#pragma once

// C++20 three-way comparison support for the containers and vocabulary types.
// Without an operator<=>, a C++20 struct that defaults its own <=> gets it
// deleted as soon as it holds a fixed_vector, fixed_string or optional (#316).
// Before C++20 none of this exists and METL_DETAIL_HAS_THREE_WAY is 0.
//
// The C++20 blocks here and at each operator<=> are fenced off from
// clang-format: the repository formats as C++17, which splits `<=>` into
// `<= >` and mangles requires-clauses.

#if defined(__cpp_impl_three_way_comparison) && __cpp_impl_three_way_comparison >= 201907L && \
    defined(__has_include)
#if __has_include(<compare>)
#include <compare>
#endif
#endif

// The language version as well as the library macro: GCC defines the
// three-way macros in C++17 mode too, where `requires` is not a keyword.
#if defined(__cpp_lib_three_way_comparison) && __cpp_lib_three_way_comparison >= 201907L && \
    defined(__cpp_concepts) && __cpp_concepts >= 201907L &&                                 \
    (__cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L))
#define METL_DETAIL_HAS_THREE_WAY 1

#include <concepts>
#include <utility>

namespace metl {
namespace detail {

// clang-format off
// synth-three-way, as the standard containers define it: the elements' own
// <=> where they have one, otherwise an ordering built from <.
struct synth_three_way_fn {
  template <typename T, typename U>
    requires requires(const T& t, const U& u) {
      { t < u } -> std::convertible_to<bool>;
      { u < t } -> std::convertible_to<bool>;
    }
  constexpr auto operator()(const T& t, const U& u) const {
    if constexpr (std::three_way_comparable_with<T, U>) {
      return t <=> u;
    } else {
      if (t < u) {
        return std::weak_ordering::less;
      }
      if (u < t) {
        return std::weak_ordering::greater;
      }
      return std::weak_ordering::equivalent;
    }
  }
};
// clang-format on

inline constexpr synth_three_way_fn synth_three_way{};

template <typename T, typename U = T>
using synth_three_way_result = decltype(synth_three_way(std::declval<const T&>(), std::declval<const U&>()));

}  // namespace detail
}  // namespace metl

#else
#define METL_DETAIL_HAS_THREE_WAY 0
#endif
