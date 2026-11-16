#pragma once

/// @file
/// @brief A functor and a value held as one member, so that an empty functor
///        takes no room (#318).
///
/// An empty member still takes a byte, and padding rounds it up to the next
/// member's alignment: an empty `std::less<>` beside a `size_t` costs 8 B on a
/// 64-bit host. An empty base costs nothing, so the pair derives from the
/// functor and holds the value.
///
/// The containers hold the pair as a member, rather than deriving from the
/// functor themselves. A base, however indirect, is one of a class's
/// associated classes for argument-dependent lookup, so a container that
/// derived from a user's comparator would bring that comparator's namespace
/// into every unqualified call on the container. A user's overloads would also
/// see a conversion from the container to the comparator, even an inaccessible
/// one.
///
/// A functor that is not empty, or is `final`, is a plain member, as it was
/// before.
///
/// The pair has no copy or move operations of its own: they are the two
/// members'. A container that copies its functor on move (#691) does that
/// through `functor()`, or holds a `functor_slot` here.
///
/// Progress guarantee: wait-free, bounded (no code at run time).

#include "metl/detail/abi.hpp"

#include <type_traits>
#include <utility>

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {

/// @brief Tag for an `ebo_pair` whose functor is value-initialized in place, as
///        a `Compare comp_;` member initialized with `comp_()` was. Passing
///        `F()` instead would need `F` to be movable.
struct value_initialized_functor_t {
  explicit value_initialized_functor_t() = default;
};

template <typename F, typename V, bool = std::is_empty_v<F> && !std::is_final_v<F>>
struct ebo_pair : private F {
  V value;

  ebo_pair() = default;

  template <typename FArg,
            typename... VArgs,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<FArg>, ebo_pair> &&
                                        !std::is_same_v<std::decay_t<FArg>, value_initialized_functor_t>>>
  constexpr explicit ebo_pair(FArg&& functor,
                              VArgs&&... args) noexcept(std::is_nothrow_constructible_v<F, FArg&&> &&
                                                        std::is_nothrow_constructible_v<V, VArgs&&...>)
      : F(std::forward<FArg>(functor)), value(std::forward<VArgs>(args)...) {}

  template <typename... VArgs>
  constexpr explicit ebo_pair(value_initialized_functor_t /*tag*/,
                              VArgs&&... args) noexcept(std::is_nothrow_default_constructible_v<F> &&
                                                        std::is_nothrow_constructible_v<V, VArgs&&...>)
      : F(), value(std::forward<VArgs>(args)...) {}

  constexpr F& functor() noexcept { return *this; }
  constexpr const F& functor() const noexcept { return *this; }
};

template <typename F, typename V>
struct ebo_pair<F, V, false> {
  F held_functor;
  V value;

  ebo_pair() = default;

  template <typename FArg,
            typename... VArgs,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<FArg>, ebo_pair> &&
                                        !std::is_same_v<std::decay_t<FArg>, value_initialized_functor_t>>>
  constexpr explicit ebo_pair(FArg&& functor,
                              VArgs&&... args) noexcept(std::is_nothrow_constructible_v<F, FArg&&> &&
                                                        std::is_nothrow_constructible_v<V, VArgs&&...>)
      : held_functor(std::forward<FArg>(functor)), value(std::forward<VArgs>(args)...) {}

  template <typename... VArgs>
  constexpr explicit ebo_pair(value_initialized_functor_t /*tag*/,
                              VArgs&&... args) noexcept(std::is_nothrow_default_constructible_v<F> &&
                                                        std::is_nothrow_constructible_v<V, VArgs&&...>)
      : held_functor(), value(std::forward<VArgs>(args)...) {}

  constexpr F& functor() noexcept { return held_functor; }
  constexpr const F& functor() const noexcept { return held_functor; }
};

}  // namespace detail
METL_DETAIL_ABI_END
}  // namespace metl
