#pragma once

/// @file
/// @brief Progress guarantees for the bit utilities (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | every function in this header | wait-free, bounded by `sizeof(T) * CHAR_BIT` |
///
/// On GCC and Clang these lower to a single instruction (`__builtin_popcount` and
/// friends). The portable software fallbacks -- which are also the constant-
/// evaluation path on MSVC, whose bit-scan intrinsics are not `constexpr` -- are
/// loops over the bits of `T`, so the worst case is the width of the type: 64
/// iterations for a `std::uint64_t`. The zero operand, which would run a
/// leading/trailing-zero loop past the width, is rejected by the public functions
/// before the loop is entered.

#include "metl/compiler.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#if METL_COMPILER_MSVC
#include <intrin.h>
#endif

namespace metl {

namespace detail {

template <typename T>
using enable_if_unsigned_integral_t = std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>, int>;

template <typename T>
inline constexpr int bit_width_v = static_cast<int>(sizeof(T) * CHAR_BIT);

// Portable, constexpr-valid software fallbacks. GCC/Clang route through the
// `__builtin_*` intrinsics (which are themselves usable in constant
// expressions); MSVC intrinsics are not constexpr, so these loops also serve as
// the compile-time path there. Each `*_impl` for leading/trailing zeros assumes
// a non-zero operand — the public functions guard the zero case first.
template <typename T>
constexpr int popcount_impl(T value) noexcept {
  int count = 0;
  while (value != 0) {
    count += static_cast<int>(value & T{1});
    value >>= 1;
  }
  return count;
}

template <typename T>
constexpr int countl_zero_impl(T value) noexcept {
  int count = 0;
  T mask = T{1} << (bit_width_v<T> - 1);
  while ((value & mask) == 0) {
    ++count;
    mask >>= 1;
  }
  return count;
}

template <typename T>
constexpr int countr_zero_impl(T value) noexcept {
  int count = 0;
  while ((value & T{1}) == 0) {
    ++count;
    value >>= 1;
  }
  return count;
}

#if METL_COMPILER_MSVC
// MSVC `_BitScan*` intrinsics cannot be used during constant evaluation. This
// lets the public functions fall back to the software loops at compile time
// while still emitting the hardware bit-scan at run time.
inline constexpr bool bit_is_constant_evaluated() noexcept {
#if METL_HAS_BUILTIN(__builtin_is_constant_evaluated) || METL_COMPILER_MSVC_VERSION >= 1925
  return __builtin_is_constant_evaluated();
#else
  return true;  // Conservative: always take the constexpr-valid software path.
#endif
}
#endif

}  // namespace detail

/// @brief Counts the number of set bits (population count) in an unsigned integer.
/// @tparam T An unsigned integral type.
/// @param value The value whose set bits are counted.
/// @return The number of 1 bits. constexpr and heap-free.
template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr int popcount(T value) noexcept {
#if METL_COMPILER_GCC || (METL_HAS_BUILTIN(__builtin_popcount) && METL_HAS_BUILTIN(__builtin_popcountll))
  // GCC/Clang `__builtin_popcount*` are constexpr-usable, so no runtime guard is
  // needed. Widening a narrower operand only introduces high zero bits, which do
  // not change the population count. Pick the builtin by operand width.
  if constexpr (detail::bit_width_v<T> <= 32) {
    return __builtin_popcount(static_cast<unsigned int>(value));
  } else {
    return __builtin_popcountll(static_cast<unsigned long long>(value));
  }
#else
  return detail::popcount_impl(value);
#endif
}

/// @brief Tests whether the value is a power of two (exactly one bit set).
/// @tparam T An unsigned integral type.
/// @param value The value to test.
/// @return true if exactly one bit is set, false otherwise (including for 0). constexpr.
template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr bool has_single_bit(T value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}

/// @brief Counts consecutive zero bits starting from the most significant bit.
/// @tparam T An unsigned integral type.
/// @param value The value to inspect.
/// @return Number of leading zero bits; the full bit width when `value` is 0. constexpr.
template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr int countl_zero(T value) noexcept {
  // `__builtin_clz*` / `_BitScanReverse*` are undefined/invalid for 0, so the
  // zero case must be handled before any intrinsic is reached.
  if (value == 0) {
    return detail::bit_width_v<T>;
  }

#if METL_COMPILER_GCC || (METL_HAS_BUILTIN(__builtin_clz) && METL_HAS_BUILTIN(__builtin_clzll))
  // `__builtin_clz` counts leading zeros in a full 32-bit `unsigned`; for a
  // narrower T the operand is zero-extended, so subtract the padding width. The
  // 64-bit variant is analogous. Both are constexpr-usable on GCC/Clang.
  if constexpr (detail::bit_width_v<T> <= 32) {
    return __builtin_clz(static_cast<unsigned int>(value)) - (32 - detail::bit_width_v<T>);
  } else {
    return __builtin_clzll(static_cast<unsigned long long>(value)) - (64 - detail::bit_width_v<T>);
  }
#elif METL_COMPILER_MSVC
  if (!detail::bit_is_constant_evaluated()) {
    unsigned long index = 0;
    if constexpr (detail::bit_width_v<T> <= 32) {
      _BitScanReverse(&index, static_cast<unsigned long>(value));
      return (detail::bit_width_v<T> - 1) - static_cast<int>(index);
    } else {
#if defined(_M_X64) || defined(_M_ARM64) || defined(_M_ARM64EC)
      _BitScanReverse64(&index, static_cast<unsigned __int64>(value));
      return (detail::bit_width_v<T> - 1) - static_cast<int>(index);
#endif
    }
  }
  return detail::countl_zero_impl(value);
#else
  return detail::countl_zero_impl(value);
#endif
}

/// @brief Counts consecutive zero bits starting from the least significant bit.
/// @tparam T An unsigned integral type.
/// @param value The value to inspect.
/// @return Number of trailing zero bits; the full bit width when `value` is 0. constexpr.
template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr int countr_zero(T value) noexcept {
  // `__builtin_ctz*` / `_BitScanForward*` are undefined/invalid for 0, so the
  // zero case must be handled before any intrinsic is reached.
  if (value == 0) {
    return detail::bit_width_v<T>;
  }

#if METL_COMPILER_GCC || (METL_HAS_BUILTIN(__builtin_ctz) && METL_HAS_BUILTIN(__builtin_ctzll))
  // Trailing-zero count is width-independent: zero-extending a narrower operand
  // adds only high zero bits and never changes the low-bit run. Both builtins
  // are constexpr-usable on GCC/Clang.
  if constexpr (detail::bit_width_v<T> <= 32) {
    return __builtin_ctz(static_cast<unsigned int>(value));
  } else {
    return __builtin_ctzll(static_cast<unsigned long long>(value));
  }
#elif METL_COMPILER_MSVC
  if (!detail::bit_is_constant_evaluated()) {
    unsigned long index = 0;
    if constexpr (detail::bit_width_v<T> <= 32) {
      _BitScanForward(&index, static_cast<unsigned long>(value));
      return static_cast<int>(index);
    } else {
#if defined(_M_X64) || defined(_M_ARM64) || defined(_M_ARM64EC)
      _BitScanForward64(&index, static_cast<unsigned __int64>(value));
      return static_cast<int>(index);
#endif
    }
  }
  return detail::countr_zero_impl(value);
#else
  return detail::countr_zero_impl(value);
#endif
}

/// @brief Number of bits needed to represent the value (position of the highest set bit).
/// @tparam T An unsigned integral type.
/// @param value The value to measure.
/// @return `1 + floor(log2(value))`, or 0 when `value` is 0. constexpr.
template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr int bit_width(T value) noexcept {
  return value == 0 ? 0 : detail::bit_width_v<T> - countl_zero(value);
}

/// @brief Largest power of two not greater than the value.
/// @tparam T An unsigned integral type.
/// @param value The value to round down.
/// @return The greatest power of two `<= value`, or 0 when `value` is 0. constexpr.
template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr T bit_floor(T value) noexcept {
  if (value == 0) {
    return 0;
  }

  return T{1} << (bit_width(value) - 1);
}

/// @brief Smallest power of two not less than the value.
/// @tparam T An unsigned integral type.
/// @param value The value to round up.
/// @return The least power of two `>= value`; 1 when `value <= 1`; 0 on overflow. constexpr.
template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr T bit_ceil(T value) noexcept {
  if (value <= 1) {
    return 1;
  }

  const T previous = static_cast<T>(value - 1);
  const int width = bit_width(previous);
  if (width >= detail::bit_width_v<T>) {
    return 0;
  }

  return T{1} << width;
}

}  // namespace metl
