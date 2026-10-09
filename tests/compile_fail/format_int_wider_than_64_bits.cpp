// EXPECT-ERROR: try_format_int takes at most a 64-bit integer
//
// The formatter works through unsigned long long. A wider integer (__int128,
// which clang and GCC treat as integral) printed "0" for 2^64, while
// try_parse_int reads the same text back correctly.

#include <metl/format.hpp>

inline bool control() {
  char buffer[24];
  return !metl::try_format_int(metl::span<char>(buffer), 42u).empty();
}

#ifdef METL_COMPILE_FAIL
inline bool formats_a_128_bit_value() {
  char buffer[48];
  return !metl::try_format_int(metl::span<char>(buffer), -(static_cast<__int128>(1) << 70)).empty();
}
#endif
