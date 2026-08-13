#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/crc.hpp"
#include "metl/span.hpp"

#include <cstddef>
#include <cstdint>

namespace metl {

/// @brief Tunable parameters for the CRC-32 computation.
struct crc32_params {
  std::uint32_t initial = 0xFFFFFFFFu;    ///< Initial CRC register value (default ISO-HDLC/zlib seed).
  std::uint32_t final_xor = 0xFFFFFFFFu;  ///< Value XORed into the CRC before it is returned.
};

namespace detail {

constexpr std::uint32_t crc32_polynomial = 0xEDB88320u;

constexpr std::uint32_t crc32_update_byte(std::uint32_t crc, std::uint8_t byte) noexcept {
#if METL_CRC_TABLE
  return crc_update_byte_reflected<std::uint32_t, crc32_polynomial>(crc, byte);
#else
  crc ^= byte;
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc & 1u) != 0u ? (crc >> 1u) ^ crc32_polynomial : (crc >> 1u);
  }
  return crc;
#endif
}

}  // namespace detail

/// @brief Computes a 32-bit CRC over a byte span (reflected polynomial 0xEDB88320, LSB-first).
/// @param bytes The bytes to checksum.
/// @param params Initial and final-XOR values (default: both 0xFFFFFFFF, i.e. standard CRC-32/zlib).
/// @return The CRC-32 checksum. constexpr and heap-free.
/// @note Uses a 16-entry nibble table by default (64 bytes of flash for this width),
///       which measured ~1.7x faster than the bit-at-a-time path on an arm64 host.
///       Set `METL_CRC_TABLE=0` for the table-free version; results are identical.
METL_NODISCARD constexpr std::uint32_t crc32(span<const std::uint8_t> bytes,
                                             crc32_params params = {}) noexcept {
  return detail::crc_fold(bytes, params.initial, params.final_xor, detail::crc32_update_byte);
}

/// @brief Computes a 32-bit CRC over a raw memory buffer.
/// @param data Pointer to the first byte.
/// @param size Number of bytes to checksum.
/// @param params Initial and final-XOR values (default: both 0xFFFFFFFF).
/// @return The CRC-32 checksum. constexpr and heap-free.
METL_NODISCARD constexpr std::uint32_t crc32(const void* data,
                                             std::size_t size,
                                             crc32_params params = {}) noexcept {
  return crc32(span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), size), params);
}

/// @brief Computes a 32-bit CRC over a NUL-terminated string (terminator excluded).
/// @param text Pointer to a NUL-terminated string.
/// @param params Initial and final-XOR values (default: both 0xFFFFFFFF).
/// @return The CRC-32 checksum. constexpr and heap-free.
METL_NODISCARD constexpr std::uint32_t crc32(const char* text, crc32_params params = {}) noexcept {
  return detail::crc_fold(text, params.initial, params.final_xor, detail::crc32_update_byte);
}

}  // namespace metl
