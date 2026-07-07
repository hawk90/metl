#pragma once

#include "metl/compiler.hpp"

#include <cstdint>
#include <type_traits>

namespace metl {

enum class endian {
  little = 0,
  big = 1,
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
  native = little,
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  native = big,
#else
  native = little,
#endif
};

namespace detail {

template <typename T>
using enable_if_integral_t = typename std::enable_if<std::is_integral<T>::value, int>::type;

template <typename T>
using unsigned_like_t = typename std::make_unsigned<T>::type;

template <typename T>
constexpr unsigned_like_t<T> byteswap_unsigned(unsigned_like_t<T> value) noexcept {
  if constexpr (sizeof(T) == 1) {
    return value;
  }

  unsigned_like_t<T> result = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    result <<= 8;
    result |= static_cast<unsigned_like_t<T>>(value & static_cast<unsigned_like_t<T>>(0xffu));
    value >>= 8;
  }
  return result;
}

}  // namespace detail

template <typename T, detail::enable_if_integral_t<T> = 0>
METL_NODISCARD constexpr T byteswap(T value) noexcept {
  using unsigned_type = detail::unsigned_like_t<T>;
  return static_cast<T>(detail::byteswap_unsigned<T>(static_cast<unsigned_type>(value)));
}

template <typename T, detail::enable_if_integral_t<T> = 0>
METL_NODISCARD constexpr T to_little_endian(T value) noexcept {
  if constexpr (sizeof(T) == 1 || endian::native == endian::little) {
    return value;
  } else {
    return byteswap(value);
  }
}

template <typename T, detail::enable_if_integral_t<T> = 0>
METL_NODISCARD constexpr T to_big_endian(T value) noexcept {
  if constexpr (sizeof(T) == 1 || endian::native == endian::big) {
    return value;
  } else {
    return byteswap(value);
  }
}

template <typename T, detail::enable_if_integral_t<T> = 0>
METL_NODISCARD constexpr T from_little_endian(T value) noexcept {
  return to_little_endian(value);
}

template <typename T, detail::enable_if_integral_t<T> = 0>
METL_NODISCARD constexpr T from_big_endian(T value) noexcept {
  return to_big_endian(value);
}

}  // namespace metl
