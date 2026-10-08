// EXPECT-ERROR: tick_extender read() must return an unsigned integer
//
// A signed reading would sign-extend on its way to 64 bits, and the top half
// of a counter's range would read as a value larger than any the counter can
// produce.

#include <cstdint>

#include <metl/tick_extender.hpp>

inline std::uint64_t unsigned_read(metl::tick_extender<16, metl::null_lock>& ext) {
  return ext.now([] { return std::uint16_t{1}; });  // control
}

#ifdef METL_COMPILE_FAIL
inline std::uint64_t signed_read(metl::tick_extender<16, metl::null_lock>& ext) {
  return ext.now([] { return std::int16_t{1}; });
}
#endif
