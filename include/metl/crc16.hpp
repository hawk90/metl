#pragma once

#include "metl/compiler.hpp"
#include "metl/span.hpp"

#include <cstddef>
#include <cstdint>

namespace metl {

/// @brief Tunable parameters for the CRC-16 computation.
struct crc16_params {
  std::uint16_t initial = 0xFFFFu;    ///< Initial CRC register value (default CRC-16/CCITT-FALSE seed).
  std::uint16_t final_xor = 0x0000u;  ///< Value XORed into the CRC before it is returned.
};

namespace detail {

constexpr std::uint16_t crc16_polynomial = 0x1021u;

constexpr std::uint16_t crc16_update_byte(std::uint16_t crc, std::uint8_t byte) noexcept {
  crc ^= static_cast<std::uint16_t>(byte) << 8u;
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc & 0x8000u) != 0u ? static_cast<std::uint16_t>((crc << 1u) ^ crc16_polynomial)
                                : static_cast<std::uint16_t>(crc << 1u);
  }
  return crc;
}

}  // namespace detail

/// @brief Computes a 16-bit CRC over a byte span (polynomial 0x1021, MSB-first, no reflection).
/// @param bytes The bytes to checksum.
/// @param params Initial and final-XOR values (default: initial 0xFFFF, final 0x0000).
/// @return The CRC-16 checksum. constexpr and heap-free.
METL_NODISCARD constexpr std::uint16_t crc16(span<const std::uint8_t> bytes,
                                             crc16_params params = {}) noexcept {
  std::uint16_t crc = params.initial;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    crc = detail::crc16_update_byte(crc, bytes[i]);
  }
  return static_cast<std::uint16_t>(crc ^ params.final_xor);
}

/// @brief Computes a 16-bit CRC over a raw memory buffer.
/// @param data Pointer to the first byte.
/// @param size Number of bytes to checksum.
/// @param params Initial and final-XOR values (default: initial 0xFFFF, final 0x0000).
/// @return The CRC-16 checksum. constexpr and heap-free.
METL_NODISCARD constexpr std::uint16_t crc16(const void* data,
                                             std::size_t size,
                                             crc16_params params = {}) noexcept {
  return crc16(span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), size), params);
}

/// @brief Computes a 16-bit CRC over a NUL-terminated string (terminator excluded).
/// @param text Pointer to a NUL-terminated string.
/// @param params Initial and final-XOR values (default: initial 0xFFFF, final 0x0000).
/// @return The CRC-16 checksum. constexpr and heap-free.
METL_NODISCARD constexpr std::uint16_t crc16(const char* text, crc16_params params = {}) noexcept {
  std::uint16_t crc = params.initial;
  for (std::size_t i = 0; text[i] != '\0'; ++i) {
    crc = detail::crc16_update_byte(crc, static_cast<std::uint8_t>(text[i]));
  }
  return static_cast<std::uint16_t>(crc ^ params.final_xor);
}

}  // namespace metl
