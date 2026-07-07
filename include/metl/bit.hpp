#pragma once

#include "metl/compiler.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace metl {

namespace detail {

template <typename T>
using enable_if_unsigned_integral_t =
    typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value, int>::type;

template <typename T>
inline constexpr int bit_width_v = static_cast<int>(sizeof(T) * CHAR_BIT);

}  // namespace detail

/// @brief Counts the number of set bits (population count) in an unsigned integer.
/// @tparam T An unsigned integral type.
/// @param value The value whose set bits are counted.
/// @return The number of 1 bits. constexpr and heap-free.
template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr int popcount(T value) noexcept {
  int count = 0;
  while (value != 0) {
    count += static_cast<int>(value & T{1});
    value >>= 1;
  }
  return count;
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
  if (value == 0) {
    return detail::bit_width_v<T>;
  }

  int count = 0;
  T mask = T{1} << (detail::bit_width_v<T> - 1);
  while ((value & mask) == 0) {
    ++count;
    mask >>= 1;
  }
  return count;
}

/// @brief Counts consecutive zero bits starting from the least significant bit.
/// @tparam T An unsigned integral type.
/// @param value The value to inspect.
/// @return Number of trailing zero bits; the full bit width when `value` is 0. constexpr.
template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr int countr_zero(T value) noexcept {
  if (value == 0) {
    return detail::bit_width_v<T>;
  }

  int count = 0;
  while ((value & T{1}) == 0) {
    ++count;
    value >>= 1;
  }
  return count;
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
