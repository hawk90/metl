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

#include "metl/config.hpp"
#include "metl/span.hpp"

#include <cstddef>
#include <cstdint>

namespace metl {
namespace detail {

/// @brief Nibble lookup table for a CRC register, built at compile time.
///
/// Four bits per step instead of one, so a byte costs two table lookups instead
/// of eight shift-and-conditional-xor iterations. Sixteen entries, so the flash
/// cost is `16 * sizeof(Crc)`: 16 bytes for CRC-8, 32 for CRC-16, 64 for CRC-32.
///
/// A 256-entry byte table would be roughly twice as fast again, but it costs
/// 1 KiB for CRC-32 alone — a poor trade on the parts this library targets, and
/// deliberately not offered until someone has the use case.
///
/// @tparam Crc Register type.
/// @tparam Polynomial Generator polynomial in the register's own bit order.
/// @tparam Reflected True for LSB-first (reflected) CRCs such as CRC-32,
///         false for MSB-first ones such as CRC-8 and CRC-16.
template <typename Crc, Crc Polynomial, bool Reflected>
struct crc_nibble_table {
  Crc entries[16];
};

/// Builds the reflected (LSB-first) nibble table.
template <typename Crc, Crc Polynomial>
constexpr crc_nibble_table<Crc, Polynomial, true> make_crc_nibble_table_reflected() noexcept {
  crc_nibble_table<Crc, Polynomial, true> table{};
  for (unsigned index = 0; index < 16u; ++index) {
    Crc value = static_cast<Crc>(index);
    for (int step = 0; step < 4; ++step) {
      value = ((value & Crc{1}) != Crc{0}) ? static_cast<Crc>((value >> 1u) ^ Polynomial)
                                           : static_cast<Crc>(value >> 1u);
    }
    table.entries[index] = value;
  }
  return table;
}

/// Builds the forward (MSB-first) nibble table.
template <typename Crc, Crc Polynomial>
constexpr crc_nibble_table<Crc, Polynomial, false> make_crc_nibble_table_forward() noexcept {
  constexpr unsigned width = sizeof(Crc) * 8u;
  constexpr Crc top_bit = static_cast<Crc>(Crc{1} << (width - 1u));

  crc_nibble_table<Crc, Polynomial, false> table{};
  for (unsigned index = 0; index < 16u; ++index) {
    Crc value = static_cast<Crc>(static_cast<Crc>(index) << (width - 4u));
    for (int step = 0; step < 4; ++step) {
      value = ((value & top_bit) != Crc{0}) ? static_cast<Crc>((value << 1u) ^ Polynomial)
                                            : static_cast<Crc>(value << 1u);
    }
    table.entries[index] = value;
  }
  return table;
}

/// One byte through a reflected nibble table.
template <typename Crc, Crc Polynomial>
constexpr Crc crc_update_byte_reflected(Crc crc, std::uint8_t byte) noexcept {
  constexpr auto table = make_crc_nibble_table_reflected<Crc, Polynomial>();
  crc = static_cast<Crc>(crc ^ byte);
  crc = static_cast<Crc>((crc >> 4u) ^ table.entries[crc & Crc{0x0F}]);
  crc = static_cast<Crc>((crc >> 4u) ^ table.entries[crc & Crc{0x0F}]);
  return crc;
}

/// One byte through a forward nibble table.
template <typename Crc, Crc Polynomial>
constexpr Crc crc_update_byte_forward(Crc crc, std::uint8_t byte) noexcept {
  constexpr unsigned width = sizeof(Crc) * 8u;
  constexpr auto table = make_crc_nibble_table_forward<Crc, Polynomial>();

  crc = static_cast<Crc>(crc ^ static_cast<Crc>(static_cast<Crc>(byte) << (width - 8u)));
  crc = static_cast<Crc>(static_cast<Crc>(crc << 4u) ^ table.entries[(crc >> (width - 4u)) & Crc{0x0F}]);
  crc = static_cast<Crc>(static_cast<Crc>(crc << 4u) ^ table.entries[(crc >> (width - 4u)) & Crc{0x0F}]);
  return crc;
}

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
