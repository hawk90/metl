// EXPECT-ERROR: unable to determine the target byte order
//
// With every byte-order macro gone, endian.hpp refuses to guess: a silent
// little-endian default would miscompile to_/from_*_endian on a big-endian
// target. The standard library and compiler.hpp come first: they need the
// macros this case removes.

#include <cstdint>
#include <type_traits>

#include <metl/compiler.hpp>

#ifdef METL_COMPILE_FAIL
#undef _WIN32
#undef __BYTE_ORDER__
#undef __LITTLE_ENDIAN__
#undef __BIG_ENDIAN__
#undef __ARMEL__
#undef __THUMBEL__
#undef __AARCH64EL__
#undef __MIPSEL__
#undef __MIPSEL
#undef _MIPSEL
#endif

#include <metl/endian.hpp>

int main() {
  return 0;
}
