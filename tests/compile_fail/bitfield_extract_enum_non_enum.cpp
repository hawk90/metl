// EXPECT-ERROR: extract_enum requires an enum type
//
// The read direction of encode_enum, and a separate assertion. Worth its own
// case because the mistake is asymmetric: `encode_enum(1)` looks wrong at the
// call site, while `extract_enum<std::uint8_t>(reg)` looks like a perfectly
// ordinary field read that happens to name the wrong type parameter.

#include <cstdint>

#include <metl/bitfield.hpp>

namespace {
enum class mode : std::uint8_t { idle = 0, run = 1 };
}  // namespace

using field = metl::bitfield<0, 2, std::uint32_t>;

mode control(std::uint32_t reg) {
  return field::extract_enum<mode>(reg);
}

#ifdef METL_COMPILE_FAIL
auto offender(std::uint32_t reg) {
  return field::extract_enum<std::uint8_t>(reg);
}
#endif
