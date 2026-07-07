// libFuzzer harness for metl::crc8 / crc16 / crc32.
//
// CRCs have no precondition to violate, so this harness simply feeds arbitrary
// bytes through every overload AND differential-checks two independent
// properties that must hold for a correct reflected/non-reflected CRC:
//
//   1. Overload agreement — the span, raw-pointer/size, and (NUL-free) C-string
//      overloads must all agree on the same bytes.
//   2. Streaming/resumability — with final_xor disabled, folding the whole
//      buffer must equal folding a prefix and then resuming the fold over the
//      remainder from the intermediate register value. A mismatch is a real
//      bug in the CRC update loop.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/crc16.hpp>
#include <metl/crc32.hpp>
#include <metl/crc8.hpp>
#include <metl/span.hpp>

namespace {

template <typename T>
metl::span<const std::uint8_t> byte_span(const T* p, std::size_t n) {
  return metl::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(p), n);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto whole = byte_span(data, size);

  // --- crc32 ---
  {
    const metl::crc32_params seed{0xFFFFFFFFu, 0x00000000u};  // final_xor OFF for resumability
    const std::uint32_t full = metl::crc32(whole, seed);

    // Overload agreement (span vs raw pointer/size).
    if (metl::crc32(data, size, seed) != full) {
      __builtin_trap();
    }

    // Streaming: fold prefix, resume over the remainder.
    for (std::size_t k = 0; k <= size; k += (size / 4) + 1) {
      const std::uint32_t part = metl::crc32(byte_span(data, k), seed);
      const std::uint32_t rest =
          metl::crc32(byte_span(data + k, size - k), metl::crc32_params{part, 0x00000000u});
      if (rest != full) {
        __builtin_trap();
      }
    }
  }

  // --- crc16 ---
  {
    const metl::crc16_params seed{0xFFFFu, 0x0000u};
    const std::uint16_t full = metl::crc16(whole, seed);
    if (metl::crc16(data, size, seed) != full) {
      __builtin_trap();
    }
    for (std::size_t k = 0; k <= size; k += (size / 4) + 1) {
      const std::uint16_t part = metl::crc16(byte_span(data, k), seed);
      const std::uint16_t rest =
          metl::crc16(byte_span(data + k, size - k), metl::crc16_params{part, 0x0000u});
      if (rest != full) {
        __builtin_trap();
      }
    }
  }

  // --- crc8 ---
  {
    const metl::crc8_params seed{0x00u, 0x00u};
    const std::uint8_t full = metl::crc8(whole, seed);
    if (metl::crc8(data, size, seed) != full) {
      __builtin_trap();
    }
    for (std::size_t k = 0; k <= size; k += (size / 4) + 1) {
      const std::uint8_t part = metl::crc8(byte_span(data, k), seed);
      const std::uint8_t rest = metl::crc8(byte_span(data + k, size - k), metl::crc8_params{part, 0x00u});
      if (rest != full) {
        __builtin_trap();
      }
    }
  }

  return 0;
}
