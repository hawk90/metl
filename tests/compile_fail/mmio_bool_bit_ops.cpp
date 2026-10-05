// EXPECT-ERROR: mmio bit operations need an unsigned integer T
//
// `bool` is a valid register type for read and write, so the type itself stays
// allowed. The bit helpers are not: they clear with `~mask`, which is always
// `true` as a bool, so `clear_bits(true)` and `modify(1, 0)` left the register
// at 1. The check sits on the bit helpers only.

#include <cstdint>

#include <metl/mmio.hpp>

void control(volatile std::uint8_t* reg) {
  const metl::mmio_ptr<std::uint8_t> p(reg);
  p.clear_bits(1);
  const metl::mmio_ptr<bool> flag(reinterpret_cast<volatile bool*>(reg));
  flag.write(true);  // read/write of a bool register still compiles
}

#ifdef METL_COMPILE_FAIL
void offender(volatile bool* reg) {
  const metl::mmio_ptr<bool> p(reg);
  p.clear_bits(true);
}
#endif
