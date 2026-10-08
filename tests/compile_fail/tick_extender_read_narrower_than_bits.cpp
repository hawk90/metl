// EXPECT-ERROR: tick_extender Bits is wider than the type read() returns
//
// A 16-bit reading wraps at 2^16. With Bits = 32 the extender expects a wrap
// at 2^32, so each 16-bit wrap -- 0xFFF0 to 0x0010, a real step of 0x20 --
// reads as a step of 0xFFFF0020, and the clock leaps ahead by nearly 2^32 with
// no assert: every value it sees is within the mask. Run time cannot tell, so
// compile time refuses it.

#include <cstdint>

#include <metl/tick_extender.hpp>

inline std::uint64_t matching_width(metl::tick_extender<16, metl::null_lock>& ext) {
  return ext.now([] { return std::uint16_t{1}; });  // control
}

#ifdef METL_COMPILE_FAIL
inline std::uint64_t narrower_read(metl::tick_extender<32, metl::null_lock>& ext) {
  return ext.now([] { return std::uint16_t{1}; });
}
#endif
