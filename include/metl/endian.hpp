#pragma once

#include "metl/compiler.hpp"

#include <cstdint>
#include <type_traits>

namespace metl {

/// @brief Byte-order enumeration with `native` bound to the target's endianness.
/// @note The native byte order is detected at compile time from `__BYTE_ORDER__` and a chain of
///       secondary compiler macros. If none resolve, the header hard-errors with `#error` rather than
///       guessing, so an undetectable target fails to compile instead of miscompiling conversions.
enum class endian {
  /// Little-endian byte order.
  little = 0,
  /// Big-endian byte order.
  big = 1,
// The `native` enumerator (defined below) equals `little` or `big` per the detected target order.
// Byte-order detection. The authoritative signal on GCC/Clang is
// __BYTE_ORDER__, which every supported cross target (arm-none-eabi,
// riscv*-elf, powerpc64) defines correctly, so a big-endian toolchain is
// detected as big-endian. A chain of well-known secondary macros covers
// compilers that omit __BYTE_ORDER__. If none of these resolve we refuse to
// guess (a silent little-endian assumption would miscompile to_/from_*_endian
// on a big-endian target) and stop the build with an actionable diagnostic.
#if defined(_WIN32)
  // Windows only runs on little-endian architectures.
  native = little,
#elif defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
  native = little,
#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  native = big,
#elif defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) || defined(__THUMBEL__) || defined(__AARCH64EL__) || \
    defined(__MIPSEL__) || defined(__MIPSEL) || defined(_MIPSEL)
  native = little,
#elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__THUMBEB__) || defined(__AARCH64EB__) || \
    defined(__MIPSEB__) || defined(__MIPSEB) || defined(_MIPSEB)
  native = big,
#else
#error \
    "metl/endian.hpp: unable to determine the target byte order. Define __BYTE_ORDER__ " \
    "(to __ORDER_LITTLE_ENDIAN__ or __ORDER_BIG_ENDIAN__) for your compiler/target."
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

/// @brief Reverses the byte order of an integral value.
/// @tparam T An integral type.
/// @param value The value whose bytes are reversed.
/// @return `value` with its bytes in reverse order (unchanged for single-byte types). constexpr.
template <typename T, detail::enable_if_integral_t<T> = 0>
METL_NODISCARD constexpr T byteswap(T value) noexcept {
  using unsigned_type = detail::unsigned_like_t<T>;
  return static_cast<T>(detail::byteswap_unsigned<T>(static_cast<unsigned_type>(value)));
}

/// @brief Converts a host-order integral value to little-endian byte order.
/// @tparam T An integral type.
/// @param value The value in native byte order.
/// @return The value in little-endian order; a no-op on little-endian targets. constexpr.
template <typename T, detail::enable_if_integral_t<T> = 0>
METL_NODISCARD constexpr T to_little_endian(T value) noexcept {
  if constexpr (sizeof(T) == 1 || endian::native == endian::little) {
    return value;
  } else {
    return byteswap(value);
  }
}

/// @brief Converts a host-order integral value to big-endian byte order.
/// @tparam T An integral type.
/// @param value The value in native byte order.
/// @return The value in big-endian order; a no-op on big-endian targets. constexpr.
template <typename T, detail::enable_if_integral_t<T> = 0>
METL_NODISCARD constexpr T to_big_endian(T value) noexcept {
  if constexpr (sizeof(T) == 1 || endian::native == endian::big) {
    return value;
  } else {
    return byteswap(value);
  }
}

/// @brief Converts a little-endian integral value to host byte order.
/// @tparam T An integral type.
/// @param value The value in little-endian order.
/// @return The value in native byte order; a no-op on little-endian targets. constexpr.
template <typename T, detail::enable_if_integral_t<T> = 0>
METL_NODISCARD constexpr T from_little_endian(T value) noexcept {
  return to_little_endian(value);
}

/// @brief Converts a big-endian integral value to host byte order.
/// @tparam T An integral type.
/// @param value The value in big-endian order.
/// @return The value in native byte order; a no-op on big-endian targets. constexpr.
template <typename T, detail::enable_if_integral_t<T> = 0>
METL_NODISCARD constexpr T from_big_endian(T value) noexcept {
  return to_big_endian(value);
}

}  // namespace metl
