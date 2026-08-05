#pragma once

/// @file
/// @brief Shared detection traits for "transparent" hashers/comparators (nested
///        `is_transparent`) plus the power-of-two `compute_bucket_count` used by
///        the open-addressing hash containers. Internal detail utilities.

#include "metl/bit.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>

namespace metl {
namespace detail {

/// @brief Detect a "transparent" functor: a hasher or comparator that declares a
///        nested `is_transparent` type, opting into heterogeneous lookup.
///
/// This is the single canonical detector shared by @c static_unordered_map,
/// @c static_unordered_set, @c flat_map, and @c flat_set (each of which used to
/// carry its own renamed copy of this SFINAE idiom).
template <typename T, typename = void>
struct has_is_transparent : false_type {};

template <typename T>
struct has_is_transparent<T, void_t<typename T::is_transparent>> : true_type {};

template <typename T>
inline constexpr bool has_is_transparent_v = has_is_transparent<T>::value;

/// @brief A hash container's lookup is transparent only when BOTH the hasher and
///        the key-equality comparator are transparent.
template <typename Hash, typename KeyEqual>
inline constexpr bool is_transparent_v = has_is_transparent_v<KeyEqual> && has_is_transparent_v<Hash>;

/// @brief Compute the bucket_count from a user-requested Capacity.
///
/// For Capacity > 0 we pick the smallest power of two that is >= Capacity * 2 (and >= 2).
/// This keeps load factor <= 50% and enables `hash & (bucket_count - 1)` instead of `% bucket_count`
/// (no hardware divide on ARM Cortex-M).
constexpr std::size_t compute_bucket_count(std::size_t capacity) noexcept {
  return capacity == 0 ? 1 : bit_ceil(capacity * 2);
}

}  // namespace detail
}  // namespace metl
