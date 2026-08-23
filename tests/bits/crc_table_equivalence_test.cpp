// The nibble-table CRC path must be byte-for-byte identical to the
// bit-at-a-time one it replaced (METL_CRC_TABLE, see config.hpp).
//
// The existing crc8/crc16/crc32 tests pin known vectors, which catches a table
// that is wrong everywhere. This catches a table that is wrong *somewhere*: it
// walks every possible input byte against a wide spread of starting register
// values, for all three widths, comparing the table step against a bitwise
// reference held here.
//
// The reference is duplicated on purpose rather than included. If it called into
// the library, a change that broke both paths the same way would still pass.

#include "metl_check.hpp"

#include <cstdint>

#include <metl/crc16.hpp>
#include <metl/crc32.hpp>
#include <metl/crc8.hpp>
#include <metl/detail/crc.hpp>

namespace {

constexpr std::uint8_t reference8(std::uint8_t crc, std::uint8_t byte) noexcept {
  // `crc ^= byte` promotes both to int and narrows on the way back, which is a
  // -Wconversion diagnostic even though the value cannot exceed 8 bits.
  crc = static_cast<std::uint8_t>(crc ^ byte);
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc & 0x80u) != 0u ? static_cast<std::uint8_t>((static_cast<unsigned>(crc) << 1u) ^ 0x07u)
                              : static_cast<std::uint8_t>(static_cast<unsigned>(crc) << 1u);
  }
  return crc;
}

constexpr std::uint16_t reference16(std::uint16_t crc, std::uint8_t byte) noexcept {
  crc = static_cast<std::uint16_t>(crc ^ static_cast<std::uint16_t>(static_cast<std::uint16_t>(byte) << 8u));
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc & 0x8000u) != 0u ? static_cast<std::uint16_t>((static_cast<unsigned>(crc) << 1u) ^ 0x1021u)
                                : static_cast<std::uint16_t>(static_cast<unsigned>(crc) << 1u);
  }
  return crc;
}

constexpr std::uint32_t reference32(std::uint32_t crc, std::uint8_t byte) noexcept {
  crc ^= byte;
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xEDB88320u : (crc >> 1u);
  }
  return crc;
}

// A spot check that also runs at compile time, so a broken table cannot even be
// built into a constexpr caller.
static_assert(reference32(0xFFFFFFFFu, 0x42u) ==
                  metl::detail::crc_update_byte_reflected<std::uint32_t, 0xEDB88320u>(0xFFFFFFFFu, 0x42u),
              "reflected nibble table must match the bitwise step at compile time");
static_assert(reference8(0x00u, 0x42u) ==
                  metl::detail::crc_update_byte_forward<std::uint8_t, 0x07u>(0x00u, 0x42u),
              "forward nibble table must match the bitwise step at compile time");

}  // namespace

int main() {
  int mismatches8 = 0;
  int mismatches16 = 0;
  int mismatches32 = 0;

  for (std::uint32_t seed = 0; seed < 65536u; seed += 7u) {
    for (int b = 0; b < 256; ++b) {
      const auto byte = static_cast<std::uint8_t>(b);

      const auto crc8_seed = static_cast<std::uint8_t>(seed);
      if (reference8(crc8_seed, byte) !=
          metl::detail::crc_update_byte_forward<std::uint8_t, 0x07u>(crc8_seed, byte)) {
        ++mismatches8;
      }

      const auto crc16_seed = static_cast<std::uint16_t>(seed);
      if (reference16(crc16_seed, byte) !=
          metl::detail::crc_update_byte_forward<std::uint16_t, 0x1021u>(crc16_seed, byte)) {
        ++mismatches16;
      }

      // Spread the 32-bit seeds across the whole register rather than only the
      // low 16 bits, so the high half of the reflected shift is exercised too.
      const std::uint32_t crc32_seed = seed * 2654435761u;
      if (reference32(crc32_seed, byte) !=
          metl::detail::crc_update_byte_reflected<std::uint32_t, 0xEDB88320u>(crc32_seed, byte)) {
        ++mismatches32;
      }
    }
  }

  CHECK_EQ(mismatches8, 0);
  CHECK_EQ(mismatches16, 0);
  CHECK_EQ(mismatches32, 0);

  // Whole-buffer agreement through the public API, whichever path is compiled in.
  {
    std::uint8_t buffer[257];
    for (std::size_t i = 0; i < sizeof buffer; ++i) {
      buffer[i] = static_cast<std::uint8_t>(i * 31u + 7u);
    }

    std::uint8_t expected8 = 0x00u;
    std::uint16_t expected16 = 0xFFFFu;
    std::uint32_t expected32 = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < sizeof buffer; ++i) {
      expected8 = reference8(expected8, buffer[i]);
      expected16 = reference16(expected16, buffer[i]);
      expected32 = reference32(expected32, buffer[i]);
    }

    CHECK_EQ(metl::crc8(buffer, sizeof buffer), expected8);
    CHECK_EQ(metl::crc16(buffer, sizeof buffer), expected16);
    CHECK_EQ(metl::crc32(buffer, sizeof buffer), static_cast<std::uint32_t>(expected32 ^ 0xFFFFFFFFu));
  }

  return metl_test::exit_code();
}
