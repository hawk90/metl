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

template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr int popcount(T value) noexcept {
  int count = 0;
  while (value != 0) {
    count += static_cast<int>(value & T{1});
    value >>= 1;
  }
  return count;
}

template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr bool has_single_bit(T value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}

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

template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr int bit_width(T value) noexcept {
  return value == 0 ? 0 : detail::bit_width_v<T> - countl_zero(value);
}

template <typename T, detail::enable_if_unsigned_integral_t<T> = 0>
METL_NODISCARD constexpr T bit_floor(T value) noexcept {
  if (value == 0) {
    return 0;
  }

  return T{1} << (bit_width(value) - 1);
}

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
