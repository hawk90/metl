// EXPECT-ERROR: encode_enum requires an enum type
//
// encode_enum exists so a field carrying a symbolic value converts through its
// underlying type once, in one place. Handed a plain integer it would silently
// become an ordinary cast -- doing the same thing while claiming, at the call
// site, that a named enumeration was involved.

#include <cstdint>

#include <metl/bitfield.hpp>

namespace {
enum class mode : std::uint8_t { idle = 0, run = 1 };
}  // namespace

using field = metl::bitfield<0, 2, std::uint32_t>;

std::uint32_t control() {
  return field::encode_enum(mode::run);
}

#ifdef METL_COMPILE_FAIL
std::uint32_t offender() {
  return field::encode_enum(1);
}
#endif
