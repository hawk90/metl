#pragma once

#include <cstddef>

namespace metl {

/// @brief Tag type selecting in-place construction of a contained value.
///
/// Passed to constructors of optional/expected to build the payload directly
/// from the given arguments (no temporary, no heap allocation).
struct in_place_t {
  explicit constexpr in_place_t() = default;
};
/// @brief Tag value of type in_place_t for in-place construction.
inline constexpr in_place_t in_place{};

/// @brief Tag type selecting the alternative of a variant by its type.
/// @tparam T The alternative type to construct in place.
template <typename T>
struct in_place_type_t {
  explicit constexpr in_place_type_t() = default;
};
/// @brief Tag value of type in_place_type_t selecting alternative by type.
/// @tparam T The alternative type to construct in place.
template <typename T>
inline constexpr in_place_type_t<T> in_place_type{};

/// @brief Tag type selecting the alternative of a variant by its index.
/// @tparam I The zero-based alternative index to construct in place.
template <std::size_t I>
struct in_place_index_t {
  explicit constexpr in_place_index_t() = default;
};
/// @brief Tag value of type in_place_index_t selecting alternative by index.
/// @tparam I The zero-based alternative index to construct in place.
template <std::size_t I>
inline constexpr in_place_index_t<I> in_place_index{};

/// @brief Tag type selecting in-place construction of an expected's error.
struct unexpect_t {
  explicit constexpr unexpect_t() = default;
};
/// @brief Tag value of type unexpect_t for in-place error construction.
inline constexpr unexpect_t unexpect{};

/// @brief Tag type denoting an empty (disengaged) optional.
struct nullopt_t {
  struct private_tag {};
  explicit constexpr nullopt_t(private_tag) noexcept {}
};
/// @brief Tag value of type nullopt_t denoting an empty optional.
inline constexpr nullopt_t nullopt{nullopt_t::private_tag{}};

/// @brief Empty regular type usable as a variant's first alternative.
///
/// Gives a variant a well-defined default-constructible state when no other
/// alternative is default-constructible. All instances compare equal.
struct monostate {};

/// @brief Equality comparison of two monostate values (always true).
constexpr bool operator==(monostate, monostate) noexcept {
  return true;
}
/// @brief Inequality comparison of two monostate values (always false).
constexpr bool operator!=(monostate, monostate) noexcept {
  return false;
}
/// @brief Less-than comparison of two monostate values (always false).
constexpr bool operator<(monostate, monostate) noexcept {
  return false;
}
/// @brief Greater-than comparison of two monostate values (always false).
constexpr bool operator>(monostate, monostate) noexcept {
  return false;
}
/// @brief Less-or-equal comparison of two monostate values (always true).
constexpr bool operator<=(monostate, monostate) noexcept {
  return true;
}
/// @brief Greater-or-equal comparison of two monostate values (always true).
constexpr bool operator>=(monostate, monostate) noexcept {
  return true;
}

}  // namespace metl
