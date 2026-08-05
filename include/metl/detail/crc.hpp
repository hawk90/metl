#pragma once

/// @file
/// @brief Algorithm-independent adapters shared by the CRC-8/16/32 headers.
///
/// The three public CRC widths (crc8/crc16/crc32) differ only in their per-byte
/// update step (polynomial, shift direction, register width). Everything else —
/// folding a byte span, folding a NUL-terminated string, and the NUL scan
/// itself — is identical. Those adapters live here, parameterized on the
/// register type and the per-width update step, so the width headers stop
/// duplicating them. Behavior is byte-for-byte identical to the hand-rolled
/// loops the width headers previously carried.

#include "metl/span.hpp"

#include <cstddef>
#include <cstdint>

namespace metl {
namespace detail {

/// @brief Length of a NUL-terminated string (terminator excluded). constexpr.
constexpr std::size_t c_string_length(const char* text) noexcept {
  std::size_t length = 0;
  while (text[length] != '\0') {
    ++length;
  }
  return length;
}

/// @brief Fold a byte span into a CRC register using a per-width update step.
/// @tparam Crc    The CRC register type (std::uint8_t / std::uint16_t / std::uint32_t).
/// @tparam Update Callable `Crc(Crc, std::uint8_t)` applying one byte to the register.
/// @return `crc ^ final_xor` after every byte has been folded in.
template <typename Crc, typename Update>
constexpr Crc crc_fold(span<const std::uint8_t> bytes, Crc initial, Crc final_xor, Update update) noexcept {
  Crc crc = initial;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    crc = update(crc, bytes[i]);
  }
  return static_cast<Crc>(crc ^ final_xor);
}

/// @brief Fold a NUL-terminated string into a CRC register (terminator excluded).
/// @tparam Crc    The CRC register type (std::uint8_t / std::uint16_t / std::uint32_t).
/// @tparam Update Callable `Crc(Crc, std::uint8_t)` applying one byte to the register.
/// @return `crc ^ final_xor` after every byte of the string has been folded in.
template <typename Crc, typename Update>
constexpr Crc crc_fold(const char* text, Crc initial, Crc final_xor, Update update) noexcept {
  const std::size_t length = c_string_length(text);
  Crc crc = initial;
  for (std::size_t i = 0; i < length; ++i) {
    crc = update(crc, static_cast<std::uint8_t>(text[i]));
  }
  return static_cast<Crc>(crc ^ final_xor);
}

}  // namespace detail
}  // namespace metl
