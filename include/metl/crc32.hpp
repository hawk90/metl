#pragma once

#include "metl/compiler.hpp"
#include "metl/span.hpp"

#include <cstddef>
#include <cstdint>

namespace metl {

struct crc32_params {
  std::uint32_t initial = 0xFFFFFFFFu;
  std::uint32_t final_xor = 0xFFFFFFFFu;
};

namespace detail {

constexpr std::uint32_t crc32_polynomial = 0xEDB88320u;

constexpr std::uint32_t crc32_update_byte(std::uint32_t crc, std::uint8_t byte) noexcept {
  crc ^= byte;
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc & 1u) != 0u ? (crc >> 1u) ^ crc32_polynomial : (crc >> 1u);
  }
  return crc;
}

constexpr std::size_t c_string_length(const char* text) noexcept {
  std::size_t length = 0;
  while (text[length] != '\0') {
    ++length;
  }
  return length;
}

}  // namespace detail

METL_NODISCARD constexpr std::uint32_t crc32(span<const std::uint8_t> bytes,
                                             crc32_params params = {}) noexcept {
  std::uint32_t crc = params.initial;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    crc = detail::crc32_update_byte(crc, bytes[i]);
  }
  return crc ^ params.final_xor;
}

METL_NODISCARD constexpr std::uint32_t crc32(const void* data,
                                             std::size_t size,
                                             crc32_params params = {}) noexcept {
  return crc32(span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), size), params);
}

METL_NODISCARD constexpr std::uint32_t crc32(const char* text, crc32_params params = {}) noexcept {
  std::uint32_t crc = params.initial;
  for (std::size_t i = 0; text[i] != '\0'; ++i) {
    crc = detail::crc32_update_byte(crc, static_cast<std::uint8_t>(text[i]));
  }
  return crc ^ params.final_xor;
}

}  // namespace metl
