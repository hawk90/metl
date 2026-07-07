#include <cstdint>

#include <metl/crc8.hpp>

namespace {

constexpr bool constexpr_checks() {
  constexpr char text[] = "123456789";
  return metl::crc8(text) == 0xF4u;
}

static_assert(constexpr_checks(), "crc8 constexpr checks failed");

}  // namespace

int main() {
  constexpr std::uint8_t payload[] = {0x01u, 0x02u, 0x03u, 0x04u};
  const auto payload_crc = metl::crc8(payload, sizeof(payload));
  if (payload_crc != 0xE3u) {
    return 1;
  }

  constexpr std::uint8_t bytes[] = {'M', 'E', 'T', 'L'};
  const auto span_crc = metl::crc8(metl::span<const std::uint8_t>(bytes, sizeof(bytes)));
  if (span_crc != 0x98u) {
    return 2;
  }

  const auto raw_crc = metl::crc8("123456789");
  if (raw_crc != 0xF4u) {
    return 3;
  }

  const auto seeded_crc = metl::crc8("123456789", metl::crc8_params{0xFFu, 0x00u});
  if (seeded_crc != 0xFBu) {
    return 4;
  }

  return 0;
}
