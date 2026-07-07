#include <cstdint>

#include <metl/bit.hpp>

namespace {

constexpr bool static_checks() {
  if (metl::popcount<std::uint32_t>(0b10110100u) != 4) {
    return false;
  }

  if (!metl::has_single_bit<std::uint32_t>(8u) || metl::has_single_bit<std::uint32_t>(10u)) {
    return false;
  }

  if (metl::countl_zero<std::uint8_t>(0x10u) != 3) {
    return false;
  }

  if (metl::countr_zero<std::uint32_t>(0b1011000u) != 3) {
    return false;
  }

  if (metl::bit_width<std::uint32_t>(17u) != 5) {
    return false;
  }

  if (metl::bit_floor<std::uint32_t>(19u) != 16u) {
    return false;
  }

  if (metl::bit_ceil<std::uint32_t>(19u) != 32u) {
    return false;
  }

  return metl::countl_zero<std::uint32_t>(0u) == 32 && metl::countr_zero<std::uint32_t>(0u) == 32;
}

static_assert(static_checks(), "bit constexpr checks failed");

}  // namespace

int main() {
  if (metl::popcount<std::uint64_t>(0xffff00000000000full) != 20) {
    return 1;
  }

  if (metl::bit_ceil<std::uint32_t>(1u) != 1u || metl::bit_floor<std::uint32_t>(1u) != 1u) {
    return 2;
  }

  if (metl::bit_floor<std::uint32_t>(0u) != 0u) {
    return 3;
  }

  if (metl::bit_ceil<std::uint32_t>(0u) != 1u) {
    return 4;
  }

  if (metl::bit_ceil<std::uint8_t>(129u) != 0u) {
    return 5;
  }

  return 0;
}
