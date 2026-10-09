#pragma once

#include <cstddef>

namespace metl {
namespace detail {

/// @brief `U` with its low `count` bits set; all ones when `count` is the full
///        width, where the plain `(U{1} << count) - 1` would shift by the width
///        (undefined).
///
/// A function rather than an inline `?:` guard: with the count a parameter,
/// MSVC no longer reports C4293 for the out-of-range shift in the arm the guard
/// never takes.
template <typename U>
constexpr U low_bits(std::size_t count) noexcept {
  if (count >= sizeof(U) * 8) {
    return static_cast<U>(~U{0});
  }
  return static_cast<U>(static_cast<U>(U{1} << count) - U{1});
}

}  // namespace detail
}  // namespace metl
