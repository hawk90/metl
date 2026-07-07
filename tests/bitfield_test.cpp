#include <cstdint>

#include <metl/bitfield.hpp>

namespace {

// CTRL register layout:
//   [31:28] MODE   (4 bits)
//   [27:16] PRESCALER (12 bits)
//   [15: 8] STATUS (8 bits)
//   [ 7: 0] FLAGS  (8 bits)
using mode_field = metl::bitfield_u32<28, 4>;
using prescaler_field = metl::bitfield_u32<16, 12>;
using status_field = metl::bitfield_u32<8, 8>;
using flags_field = metl::bitfield_u32<0, 8>;

constexpr bool static_checks() {
  if (mode_field::mask != 0xf0000000u) {
    return false;
  }
  if (prescaler_field::mask != 0x0fff0000u) {
    return false;
  }
  if (status_field::mask != 0x0000ff00u) {
    return false;
  }
  if (flags_field::mask != 0x000000ffu) {
    return false;
  }

  // extract: pull the field out of a register snapshot.
  constexpr std::uint32_t reg = 0xa5cd1234u;
  if (mode_field::extract(reg) != 0xau) {
    return false;
  }
  if (prescaler_field::extract(reg) != 0x5cdu) {
    return false;
  }
  if (status_field::extract(reg) != 0x12u) {
    return false;
  }
  if (flags_field::extract(reg) != 0x34u) {
    return false;
  }

  // encode: shift a value to the field's position, masked.
  if (mode_field::encode(0xau) != 0xa0000000u) {
    return false;
  }
  // Extra high bits in the input must be discarded by the mask.
  if (mode_field::encode(0xffu) != 0xf0000000u) {
    return false;
  }

  // insert: replace only the selected field, preserve others.
  constexpr std::uint32_t base = 0x12345678u;
  if (mode_field::insert(base, 0x7u) != 0x72345678u) {
    return false;
  }
  if (flags_field::insert(base, 0xaau) != 0x123456aau) {
    return false;
  }

  // Full-width field: 32-bit-wide bitfield over a 32-bit register.
  using full_field = metl::bitfield_u32<0, 32>;
  if (full_field::mask != 0xffffffffu) {
    return false;
  }
  if (full_field::extract(0xdeadbeefu) != 0xdeadbeefu) {
    return false;
  }

  // u8 alias.
  using nibble_field = metl::bitfield_u8<4, 4>;
  if (nibble_field::mask != 0xf0u) {
    return false;
  }
  if (nibble_field::extract(static_cast<std::uint8_t>(0xa5u)) != 0xau) {
    return false;
  }

  // u64 alias with high LSB.
  using high_field = metl::bitfield_u64<56, 8>;
  if (high_field::mask != 0xff00000000000000ull) {
    return false;
  }
  if (high_field::extract(0xab00000000000000ull) != 0xabull) {
    return false;
  }

  return true;
}

static_assert(static_checks(), "bitfield constexpr checks failed");

enum class clock_source : std::uint32_t {
  internal = 0,
  external = 1,
  pll = 2,
  reserved = 3,
};

using clksrc_field = metl::bitfield_u32<24, 2>;

constexpr bool enum_checks() {
  if (clksrc_field::encode_enum(clock_source::pll) != 0x02000000u) {
    return false;
  }
  if (clksrc_field::extract_enum<clock_source>(0x02000000u) != clock_source::pll) {
    return false;
  }
  return true;
}

static_assert(enum_checks(), "bitfield enum checks failed");

}  // namespace

int main() {
  // Runtime confirmation, in case host evaluates differently than constexpr.
  std::uint32_t reg = 0u;
  reg = mode_field::insert(reg, 0x3u);
  reg = prescaler_field::insert(reg, 0xabcu);
  reg = flags_field::insert(reg, 0x42u);

  if (mode_field::extract(reg) != 0x3u) {
    return 1;
  }
  if (prescaler_field::extract(reg) != 0xabcu) {
    return 2;
  }
  if (flags_field::extract(reg) != 0x42u) {
    return 3;
  }
  if (status_field::extract(reg) != 0u) {
    return 4;
  }

  return 0;
}
