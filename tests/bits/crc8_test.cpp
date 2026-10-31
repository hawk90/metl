#include <cstddef>
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

  // The same string in a buffer filled at run time, as a received one would
  // be. The literal calls above initialize const locals, which the compiler
  // evaluates as constants, so only this runs the NUL scan and the C-string
  // fold at run time.
  char received[10] = {};
  for (std::size_t i = 0; i < 9; ++i) {
    received[i] = static_cast<char>('1' + i);
  }
  if (metl::crc8(received) != 0xF4u) {
    return 5;
  }

  // Chaining: the rule stated on crc8_params, at every split of the message
  // (both empty parts included) and for a final_xor of zero and of non-zero.
  const std::uint8_t message[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  const metl::crc8_params all_params[] = {
      metl::crc8_params{}, metl::crc8_params{0x5Au, 0x00u}, metl::crc8_params{0x5Au, 0xA5u}};
  for (const metl::crc8_params& params : all_params) {
    const auto whole = metl::crc8(message, sizeof(message), params);
    for (std::size_t split = 0; split <= sizeof(message); ++split) {
      const auto first = metl::crc8(message, split, params);
      const metl::crc8_params next{static_cast<std::uint8_t>(first ^ params.final_xor), params.final_xor};
      if (metl::crc8(message + split, sizeof(message) - split, next) != whole) {
        return 90;
      }
    }
  }

  return 0;
}
