// The CRC loops keep their nibble tables out of the stack frame.
//
// With each table a `constexpr` local, GCC 16 at -O2 and -O3 copied it onto the
// stack inside the per-byte loop: a 64-byte frame and four vector stores per
// byte for CRC-32, 32 bytes for CRC-16, 16 for CRC-8. The results were right,
// so no test noticed. -Os was not affected, which is why the ARM stack probe,
// built at -Os, could not see it either.
//
// cmake/CheckStackFrames.cmake reads this TU's .su file. It is compiled at -O2,
// not linked, and not instrumented, so each frame is the loop's own: a few
// callee-saved registers at most, and under the 32 bytes the CRC-16 table would
// add.

#include <cstddef>
#include <cstdint>

#include <metl/crc16.hpp>
#include <metl/crc32.hpp>
#include <metl/crc8.hpp>
#include <metl/span.hpp>

extern "C" std::uint32_t metl_probe_crc32(const std::uint8_t* data, std::size_t size) {
  return metl::crc32(metl::span<const std::uint8_t>(data, size));
}

extern "C" std::uint16_t metl_probe_crc16(const std::uint8_t* data, std::size_t size) {
  return metl::crc16(metl::span<const std::uint8_t>(data, size));
}

extern "C" std::uint8_t metl_probe_crc8(const std::uint8_t* data, std::size_t size) {
  return metl::crc8(metl::span<const std::uint8_t>(data, size));
}
