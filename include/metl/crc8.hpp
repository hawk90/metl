#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/crc.hpp"
#include "metl/span.hpp"

#include <cstddef>
#include <cstdint>

namespace metl {

/// @brief Tunable parameters for the CRC-8 computation.
struct crc8_params {
  std::uint8_t initial = 0x00u;    ///< Initial CRC register value.
  std::uint8_t final_xor = 0x00u;  ///< Value XORed into the CRC before it is returned.
};

namespace detail {

constexpr std::uint8_t crc8_polynomial = 0x07u;

constexpr std::uint8_t crc8_update_byte(std::uint8_t crc, std::uint8_t byte) noexcept {
#if METL_CRC_TABLE
  return crc_update_byte_forward<std::uint8_t, crc8_polynomial>(crc, byte);
#else
  crc ^= byte;
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc & 0x80u) != 0u ? static_cast<std::uint8_t>((crc << 1u) ^ crc8_polynomial)
                              : static_cast<std::uint8_t>(crc << 1u);
  }
  return crc;
#endif
}

}  // namespace detail

/// @brief Computes an 8-bit CRC over a byte span (polynomial 0x07, MSB-first, no reflection).
/// @param bytes The bytes to checksum.
/// @param params Initial and final-XOR values (default: both 0x00).
/// @return The CRC-8 checksum. constexpr and heap-free.
/// @note Uses a 16-entry nibble table by default (16 bytes of flash for this width).
///       Set `METL_CRC_TABLE=0` for the table-free version; results are identical.
METL_NODISCARD constexpr std::uint8_t crc8(span<const std::uint8_t> bytes, crc8_params params = {}) noexcept {
  return detail::crc_fold(bytes, params.initial, params.final_xor, detail::crc8_update_byte);
}

/// @brief Computes an 8-bit CRC over a raw memory buffer.
/// @param data Pointer to the first byte.
/// @param size Number of bytes to checksum.
/// @param params Initial and final-XOR values (default: both 0x00).
/// @return The CRC-8 checksum. constexpr and heap-free.
METL_NODISCARD constexpr std::uint8_t crc8(const void* data,
                                           std::size_t size,
                                           crc8_params params = {}) noexcept {
  return crc8(span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), size), params);
}

/// @brief Computes an 8-bit CRC over a NUL-terminated string (terminator excluded).
/// @param text Pointer to a NUL-terminated string.
/// @param params Initial and final-XOR values (default: both 0x00).
/// @return The CRC-8 checksum. constexpr and heap-free.
METL_NODISCARD constexpr std::uint8_t crc8(const char* text, crc8_params params = {}) noexcept {
  return detail::crc_fold(text, params.initial, params.final_xor, detail::crc8_update_byte);
}

}  // namespace metl
