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

  // --- Edge cases: zero, all-ones, single MSB/LSB bit, per supported width ---

  // popcount at the extremes for every width.
  if (metl::popcount<std::uint8_t>(0u) != 0 || metl::popcount<std::uint8_t>(0xffu) != 8) {
    return false;
  }
  if (metl::popcount<std::uint16_t>(0u) != 0 || metl::popcount<std::uint16_t>(0xffffu) != 16) {
    return false;
  }
  if (metl::popcount<std::uint32_t>(0u) != 0 || metl::popcount<std::uint32_t>(0xffffffffu) != 32) {
    return false;
  }
  if (metl::popcount<std::uint64_t>(0u) != 0 || metl::popcount<std::uint64_t>(0xffffffffffffffffull) != 64) {
    return false;
  }

  // countl_zero: 0 -> full width, single MSB -> 0, single LSB -> width-1.
  if (metl::countl_zero<std::uint8_t>(0u) != 8 || metl::countl_zero<std::uint8_t>(0x80u) != 0 ||
      metl::countl_zero<std::uint8_t>(0x01u) != 7) {
    return false;
  }
  if (metl::countl_zero<std::uint16_t>(0u) != 16 || metl::countl_zero<std::uint16_t>(0x8000u) != 0 ||
      metl::countl_zero<std::uint16_t>(0x0001u) != 15) {
    return false;
  }
  if (metl::countl_zero<std::uint32_t>(0u) != 32 || metl::countl_zero<std::uint32_t>(0x80000000u) != 0 ||
      metl::countl_zero<std::uint32_t>(0x00000001u) != 31) {
    return false;
  }
  if (metl::countl_zero<std::uint64_t>(0u) != 64 ||
      metl::countl_zero<std::uint64_t>(0x8000000000000000ull) != 0 ||
      metl::countl_zero<std::uint64_t>(0x0000000000000001ull) != 63) {
    return false;
  }

  // countr_zero: 0 -> full width, single LSB -> 0, single MSB -> width-1.
  if (metl::countr_zero<std::uint8_t>(0u) != 8 || metl::countr_zero<std::uint8_t>(0x01u) != 0 ||
      metl::countr_zero<std::uint8_t>(0x80u) != 7) {
    return false;
  }
  if (metl::countr_zero<std::uint16_t>(0u) != 16 || metl::countr_zero<std::uint16_t>(0x0001u) != 0 ||
      metl::countr_zero<std::uint16_t>(0x8000u) != 15) {
    return false;
  }
  if (metl::countr_zero<std::uint32_t>(0u) != 32 || metl::countr_zero<std::uint32_t>(0x00000001u) != 0 ||
      metl::countr_zero<std::uint32_t>(0x80000000u) != 31) {
    return false;
  }
  if (metl::countr_zero<std::uint64_t>(0u) != 64 ||
      metl::countr_zero<std::uint64_t>(0x0000000000000001ull) != 0 ||
      metl::countr_zero<std::uint64_t>(0x8000000000000000ull) != 63) {
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

  // Exercise the runtime (non-constant-evaluated) intrinsic dispatch: `volatile`
  // inputs prevent constant folding, so on MSVC the `_BitScan*` path is taken
  // and on GCC/Clang the builtins run at run time.
  volatile std::uint8_t v8 = 0x80u;
  volatile std::uint16_t v16 = 0x8000u;
  volatile std::uint32_t v32 = 0x80000000u;
  volatile std::uint64_t v64 = 0x8000000000000000ull;

  if (metl::countl_zero<std::uint8_t>(v8) != 0 || metl::countr_zero<std::uint8_t>(v8) != 7 ||
      metl::popcount<std::uint8_t>(v8) != 1) {
    return 6;
  }
  if (metl::countl_zero<std::uint16_t>(v16) != 0 || metl::countr_zero<std::uint16_t>(v16) != 15 ||
      metl::popcount<std::uint16_t>(v16) != 1) {
    return 7;
  }
  if (metl::countl_zero<std::uint32_t>(v32) != 0 || metl::countr_zero<std::uint32_t>(v32) != 31 ||
      metl::popcount<std::uint32_t>(v32) != 1) {
    return 8;
  }
  if (metl::countl_zero<std::uint64_t>(v64) != 0 || metl::countr_zero<std::uint64_t>(v64) != 63 ||
      metl::popcount<std::uint64_t>(v64) != 1) {
    return 9;
  }

  volatile std::uint32_t vzero = 0u;
  if (metl::countl_zero<std::uint32_t>(vzero) != 32 || metl::countr_zero<std::uint32_t>(vzero) != 32 ||
      metl::popcount<std::uint32_t>(vzero) != 0) {
    return 10;
  }

  volatile std::uint32_t vall = 0xffffffffu;
  if (metl::countl_zero<std::uint32_t>(vall) != 0 || metl::countr_zero<std::uint32_t>(vall) != 0 ||
      metl::popcount<std::uint32_t>(vall) != 32) {
    return 11;
  }

  return 0;
}
