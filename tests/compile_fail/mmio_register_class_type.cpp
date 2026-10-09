// EXPECT-ERROR: mmio_register requires a scalar register type
//
// As mmio_ptr: a trivially copyable struct cannot be copied from a volatile
// lvalue, so it was accepted here and then failed inside read_once/write_once.

#include <cstdint>

#include <metl/mmio.hpp>

namespace {

struct overlay {
  std::uint32_t value;
};

}  // namespace

using control_reg = metl::mmio_register<std::uint32_t, 0x40000000>;
static_assert(control_reg::address != 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender_reg = metl::mmio_register<overlay, 0x40000000>;
static_assert(offender_reg::address != 0, "forces the instantiation");
#endif
