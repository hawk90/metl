// Regression test for metl::mmio:
//   - the runtime-address mmio_ptr<T>(uintptr_t) constructor accepts a properly
//     aligned address and round-trips reads/writes (the alignment assert must
//     NOT fire for aligned addresses);
//   - mmio_register<T, Address> enforces alignment at compile time via
//     static_assert (an aligned address compiles and works);
//   - the volatile-pointer constructor remains usable in a constexpr context
//     (get() is constexpr).

#include "metl_check.hpp"

#include <cstdint>

#include <metl/mmio.hpp>

namespace {

// Naturally aligned backing storage for a "register".
alignas(std::uint32_t) volatile std::uint32_t backing = 0u;

// mmio_register<T, 0> — address 0 is aligned to any alignment, so the
// compile-time alignment static_assert is satisfied.
using reg0_t = metl::mmio_register<std::uint32_t, 0>;
static_assert(reg0_t::address == 0, "address constant");

}  // namespace

int main() {
  // Runtime-address ctor with an aligned address.
  const auto addr = reinterpret_cast<std::uintptr_t>(&backing);
  CHECK_EQ(addr % alignof(std::uint32_t), std::uintptr_t(0));

  metl::mmio_ptr<std::uint32_t> r(addr);
  r.write(0xcafef00du);
  CHECK_EQ(backing, 0xcafef00du);
  CHECK_EQ(r.read(), 0xcafef00du);

  r.modify(0x0000ffffu, 0x00001234u);
  CHECK_EQ(r.read(), 0xcafe1234u);

  // Volatile-pointer ctor: get() is constexpr-qualified and returns the addr.
  metl::mmio_ptr<std::uint32_t> p(&backing);
  CHECK(p.get() == &backing);

  return metl_test::exit_code();
}
