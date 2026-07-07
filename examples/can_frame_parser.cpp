// can_frame_parser.cpp
//
// Parse a CAN-like wire frame from a buffer of bytes, demonstrating:
//
//   - metl::span<const std::byte>     — non-owning byte view
//   - metl::bitfield                  — compile-time field extraction
//   - metl::crc16                     — bounded-time CRC validation
//   - metl::expected<frame, error>    — error-free error propagation
//
// The wire layout is a synthetic but plausible CAN-FD-ish frame:
//
//   offset  size  field
//   ------  ----  -----------------------------------------------------------
//      0      4   header (uint32 LE, big-endian on the wire would be similar)
//                   bits [31:21]  ID         (11 bits)
//                   bits [20:17]  DLC        (4 bits — data length code)
//                   bits [16]     RTR        (1 bit — remote transmission)
//                   bits [15]     IDE        (1 bit — extended ID)
//                   bits [14:0]   reserved
//      4      N   payload (N == DLC bytes, N in [0..8])
//   4+N      2   CRC-16 over header+payload (uint16 LE)
//
// The example builds a frame, hands the buffer to the parser, and checks
// every observable property.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <metl/bitfield.hpp>
#include <metl/crc16.hpp>
#include <metl/expected.hpp>
#include <metl/span.hpp>

namespace {

// Header field layout. Total width is 32 bits.
using id_field = metl::bitfield_u32<21, 11>;
using dlc_field = metl::bitfield_u32<17, 4>;
using rtr_field = metl::bitfield_u32<16, 1>;
using ide_field = metl::bitfield_u32<15, 1>;

struct can_frame {
  std::uint16_t id;
  std::uint8_t dlc;
  bool rtr;
  bool ide;
  std::uint8_t payload[8];
};

enum class parse_error : std::uint8_t {
  too_short,
  bad_dlc,
  bad_crc,
};

constexpr std::size_t kHeaderSize = 4;
constexpr std::size_t kCrcSize = 2;

std::uint32_t load_u32_le(const std::byte* p) noexcept {
  return static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[0])) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[2])) << 16) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[3])) << 24);
}

std::uint16_t load_u16_le(const std::byte* p) noexcept {
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[0])) |
                                    (static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[1])) << 8));
}

void store_u32_le(std::byte* p, std::uint32_t v) noexcept {
  p[0] = static_cast<std::byte>(v & 0xffu);
  p[1] = static_cast<std::byte>((v >> 8) & 0xffu);
  p[2] = static_cast<std::byte>((v >> 16) & 0xffu);
  p[3] = static_cast<std::byte>((v >> 24) & 0xffu);
}

void store_u16_le(std::byte* p, std::uint16_t v) noexcept {
  p[0] = static_cast<std::byte>(v & 0xffu);
  p[1] = static_cast<std::byte>((v >> 8) & 0xffu);
}

metl::expected<can_frame, parse_error> parse_frame(metl::span<const std::byte> buf) noexcept {
  if (buf.size() < kHeaderSize + kCrcSize) {
    return metl::unexpected<parse_error>(parse_error::too_short);
  }

  const std::uint32_t header = load_u32_le(buf.data());

  can_frame f{};
  f.id = static_cast<std::uint16_t>(id_field::extract(header));
  f.dlc = static_cast<std::uint8_t>(dlc_field::extract(header));
  f.rtr = rtr_field::extract(header) != 0u;
  f.ide = ide_field::extract(header) != 0u;

  if (f.dlc > 8) {
    return metl::unexpected<parse_error>(parse_error::bad_dlc);
  }

  if (buf.size() < kHeaderSize + f.dlc + kCrcSize) {
    return metl::unexpected<parse_error>(parse_error::too_short);
  }

  for (std::uint8_t i = 0; i < f.dlc; ++i) {
    f.payload[i] = static_cast<std::uint8_t>(buf[kHeaderSize + i]);
  }

  // CRC covers header + payload.
  const std::size_t crc_offset = kHeaderSize + f.dlc;
  const std::uint16_t wire_crc = load_u16_le(buf.data() + crc_offset);

  // metl::crc16 currently consumes a uint8_t span; create one transiently
  // from our byte view. The two types are layout-compatible.
  metl::span<const std::uint8_t> covered(reinterpret_cast<const std::uint8_t*>(buf.data()), crc_offset);
  const std::uint16_t computed = metl::crc16(covered);
  if (wire_crc != computed) {
    return metl::unexpected<parse_error>(parse_error::bad_crc);
  }

  return f;
}

// Pack a frame back to a buffer; returns total size written.
std::size_t encode_frame(const can_frame& f, std::byte* out, std::size_t cap) noexcept {
  const std::size_t total = kHeaderSize + f.dlc + kCrcSize;
  if (cap < total)
    return 0;

  std::uint32_t header = 0;
  header = id_field::insert(header, f.id);
  header = dlc_field::insert(header, f.dlc);
  header = rtr_field::insert(header, f.rtr ? 1u : 0u);
  header = ide_field::insert(header, f.ide ? 1u : 0u);
  store_u32_le(out, header);

  for (std::uint8_t i = 0; i < f.dlc; ++i) {
    out[kHeaderSize + i] = static_cast<std::byte>(f.payload[i]);
  }

  const std::size_t crc_offset = kHeaderSize + f.dlc;
  metl::span<const std::uint8_t> covered(reinterpret_cast<const std::uint8_t*>(out), crc_offset);
  const std::uint16_t crc = metl::crc16(covered);
  store_u16_le(out + crc_offset, crc);

  return total;
}

}  // namespace

int main() {
  // ---- Build a valid frame and round-trip parse it ------------------------
  can_frame src{};
  src.id = 0x123;
  src.dlc = 6;
  src.rtr = false;
  src.ide = true;
  const std::uint8_t kPayload[6] = {0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe};
  std::memcpy(src.payload, kPayload, sizeof(kPayload));

  std::array<std::byte, 32> buf{};
  const std::size_t n = encode_frame(src, buf.data(), buf.size());
  if (n == 0)
    return 1;

  auto parsed = parse_frame(metl::span<const std::byte>(buf.data(), n));
  if (!parsed)
    return 2;
  if (parsed->id != src.id)
    return 3;
  if (parsed->dlc != src.dlc)
    return 4;
  if (parsed->rtr != src.rtr)
    return 5;
  if (parsed->ide != src.ide)
    return 6;
  for (std::uint8_t i = 0; i < src.dlc; ++i) {
    if (parsed->payload[i] != src.payload[i])
      return 7;
  }

  // ---- Truncated buffer triggers too_short --------------------------------
  {
    auto e = parse_frame(metl::span<const std::byte>(buf.data(), 3));
    if (e || e.error() != parse_error::too_short)
      return 8;
  }

  // ---- Corrupt the payload to break CRC -----------------------------------
  {
    auto corrupt = buf;
    corrupt[kHeaderSize + 0] = static_cast<std::byte>(0x00);  // flip one byte
    auto e = parse_frame(metl::span<const std::byte>(corrupt.data(), n));
    if (e || e.error() != parse_error::bad_crc)
      return 9;
  }

  // ---- DLC > 8 triggers bad_dlc ------------------------------------------
  {
    std::array<std::byte, 32> bad{};
    // Build a header with DLC = 15.
    std::uint32_t h = 0;
    h = id_field::insert(h, 0x1u);
    h = dlc_field::insert(h, 15u);
    store_u32_le(bad.data(), h);
    // Append two filler CRC bytes (parser checks DLC before the size
    // recheck for the payload extent).
    store_u16_le(bad.data() + kHeaderSize, 0u);
    auto e = parse_frame(metl::span<const std::byte>(bad.data(), kHeaderSize + kCrcSize));
    if (e || e.error() != parse_error::bad_dlc)
      return 10;
  }

  std::printf("can_frame_parser: id=0x%03x dlc=%u ide=%d rtr=%d crc=ok\n",
              static_cast<unsigned>(parsed->id),
              static_cast<unsigned>(parsed->dlc),
              parsed->ide ? 1 : 0,
              parsed->rtr ? 1 : 0);
  return 0;
}
