#include <cstddef>
#include <cstdint>

#include <metl/crc32.hpp>

namespace {

constexpr bool constexpr_checks() {
  constexpr char text[] = "123456789";
  return metl::crc32(text) == 0xCBF43926u;
}

static_assert(constexpr_checks(), "crc32 constexpr checks failed");

}  // namespace

int main() {
  constexpr std::uint8_t payload[] = {0x01u, 0x02u, 0x03u, 0x04u};
  const auto payload_crc = metl::crc32(payload, sizeof(payload));
  if (payload_crc != 0xB63CFBCD) {
    return 1;
  }

  constexpr std::uint8_t bytes[] = {'M', 'E', 'T', 'L'};
  const auto span_crc = metl::crc32(metl::span<const std::uint8_t>(bytes, sizeof(bytes)));
  if (span_crc != 0x9FD6853Cu) {
    return 2;
  }

  const auto raw_crc = metl::crc32("123456789");
  if (raw_crc != 0xCBF43926u) {
    return 3;
  }

  const auto seeded_crc = metl::crc32("123456789", metl::crc32_params{0u, 0u});
  if (seeded_crc != 0x2DFD2D88u) {
    return 4;
  }

  // The same string in a buffer filled at run time, as a received one would
  // be. The literal calls above initialize const locals, which the compiler
  // evaluates as constants, so only this runs the NUL scan and the C-string
  // fold at run time.
  char received[10] = {};
  for (std::size_t i = 0; i < 9; ++i) {
    received[i] = static_cast<char>('1' + i);
  }
  if (metl::crc32(received) != 0xCBF43926u) {
    return 5;
  }

  return 0;
}
