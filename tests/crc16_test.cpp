#include <cstdint>

#include <metl/crc16.hpp>

namespace {

constexpr bool constexpr_checks() {
  constexpr char text[] = "123456789";
  return metl::crc16(text) == 0x29B1u;
}

static_assert(constexpr_checks(), "crc16 constexpr checks failed");

}  // namespace

int main() {
  constexpr std::uint8_t payload[] = {0x01u, 0x02u, 0x03u, 0x04u};
  const auto payload_crc = metl::crc16(payload, sizeof(payload));
  if (payload_crc != 0x89C3u) {
    return 1;
  }

  constexpr std::uint8_t bytes[] = {'M', 'E', 'T', 'L'};
  const auto span_crc = metl::crc16(metl::span<const std::uint8_t>(bytes, sizeof(bytes)));
  if (span_crc != 0x6EB4u) {
    return 2;
  }

  const auto raw_crc = metl::crc16("123456789");
  if (raw_crc != 0x29B1u) {
    return 3;
  }

  const auto seeded_crc = metl::crc16("123456789", metl::crc16_params{0u, 0u});
  if (seeded_crc != 0x31C3u) {
    return 4;
  }

  return 0;
}
