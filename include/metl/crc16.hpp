#pragma once

/// @file
/// @brief Progress guarantees for the CRC-16 functions (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | one byte, table build (`METL_CRC_TABLE` on) | wait-free, bounded |
///   | one byte, bitwise (`METL_CRC_TABLE` off) | wait-free, 8 iterations |
///   | a buffer | wait-free, bounded by the length you pass |
///
/// Per-byte cost is a compile-time constant either way -- one table lookup, or a
/// fixed eight shift-and-conditional-xor steps. Whole-buffer cost is that constant
/// times the length, and the length is the caller's, so a caller with a deadline
/// bounds it by choosing how much to feed in at a time.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/crc.hpp"
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
#if METL_CRC_TABLE
  return crc_update_byte_forward<std::uint16_t, crc16_polynomial>(crc, byte);
#else
  crc ^= static_cast<std::uint16_t>(byte) << 8u;
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc & 0x8000u) != 0u ? static_cast<std::uint16_t>((crc << 1u) ^ crc16_polynomial)
                                : static_cast<std::uint16_t>(crc << 1u);
  }
  return crc;
#endif
}

}  // namespace detail

/// @brief Computes a 16-bit CRC over a byte span (polynomial 0x1021, MSB-first, no reflection).
/// @param bytes The bytes to checksum.
/// @param params Initial and final-XOR values (default: initial 0xFFFF, final 0x0000).
/// @return The CRC-16 checksum. constexpr and heap-free.
/// @note Uses a 16-entry nibble table by default (32 bytes of flash for this width).
///       Set `METL_CRC_TABLE=0` for the table-free version; results are identical.
METL_NODISCARD constexpr std::uint16_t crc16(span<const std::uint8_t> bytes,
                                             crc16_params params = {}) noexcept {
  return detail::crc_fold(bytes, params.initial, params.final_xor, detail::crc16_update_byte);
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
  return detail::crc_fold(text, params.initial, params.final_xor, detail::crc16_update_byte);
}

}  // namespace metl
