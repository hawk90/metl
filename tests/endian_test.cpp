#include <cstdint>

#include <metl/endian.hpp>

namespace {

constexpr bool static_checks() {
  if (metl::byteswap<std::uint16_t>(0x1234u) != 0x3412u) {
    return false;
  }

  if (metl::byteswap<std::uint32_t>(0x12345678u) != 0x78563412u) {
    return false;
  }

  if (metl::byteswap<std::uint8_t>(0xa5u) != 0xa5u) {
    return false;
  }

  if (metl::from_little_endian<std::uint32_t>(metl::to_little_endian<std::uint32_t>(0x11223344u)) !=
      0x11223344u) {
    return false;
  }

  return metl::from_big_endian<std::uint32_t>(metl::to_big_endian<std::uint32_t>(0x55667788u)) == 0x55667788u;
}

static_assert(static_checks(), "endian constexpr checks failed");

}  // namespace

int main() {
  if (metl::byteswap<std::uint64_t>(0x0102030405060708ull) != 0x0807060504030201ull) {
    return 1;
  }

  if (metl::from_little_endian<std::int32_t>(metl::to_little_endian<std::int32_t>(0x12345678)) !=
      0x12345678) {
    return 2;
  }

  if (metl::from_big_endian<std::int16_t>(metl::to_big_endian<std::int16_t>(0x1234)) != 0x1234) {
    return 3;
  }

  if constexpr (metl::endian::native == metl::endian::little) {
    if (metl::to_little_endian<std::uint32_t>(0x01020304u) != 0x01020304u) {
      return 4;
    }
    if (metl::to_big_endian<std::uint32_t>(0x01020304u) != 0x04030201u) {
      return 5;
    }
  } else {
    if (metl::to_big_endian<std::uint32_t>(0x01020304u) != 0x01020304u) {
      return 6;
    }
    if (metl::to_little_endian<std::uint32_t>(0x01020304u) != 0x04030201u) {
      return 7;
    }
  }

  return 0;
}
