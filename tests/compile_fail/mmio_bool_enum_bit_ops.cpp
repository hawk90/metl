// EXPECT-ERROR: mmio bit operations need an unsigned integer T
//
// An enum with a bool underlying type is a register type for read and write,
// as bool is. The bit helpers check the underlying type, not the enum: on bool
// bits `~mask` is always true, so `clear_bits(F::on)` left the register at 1
// (#576).

#include <cstdint>

#include <metl/mmio.hpp>

enum class flag : bool { off = false, on = true };
enum class wide : std::uint8_t { bit0 = 1 };

void control(volatile flag* reg, volatile wide* other) {
  const metl::mmio_ptr<flag> f(reg);
  f.write(flag::on);  // read/write of a bool-backed enum still compiles
  const metl::mmio_ptr<wide> w(other);
  w.clear_bits(wide::bit0);  // an integer-backed enum keeps its bit helpers
}

#ifdef METL_COMPILE_FAIL
void offender(volatile flag* reg) {
  const metl::mmio_ptr<flag> p(reg);
  p.clear_bits(flag::on);
}
#endif
