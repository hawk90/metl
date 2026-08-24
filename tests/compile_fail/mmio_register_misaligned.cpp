// EXPECT-ERROR: mmio_register address must be aligned to alignof(T)
//
// An unaligned MMIO access is not a slow access -- on Cortex-M it is a
// HardFault, and on a peripheral bus it can be a fault the core reports at a
// completely unrelated instruction. The address is a template argument, so the
// only place this can be caught for free is here.

#include <cstdint>

#include <metl/mmio.hpp>

using aligned = metl::mmio_register<std::uint32_t, 0x40000000>;
static_assert(aligned::address == 0x40000000, "control instantiation");

#ifdef METL_COMPILE_FAIL
using misaligned = metl::mmio_register<std::uint32_t, 0x40000002>;
static_assert(misaligned::address == 0x40000002, "forces the instantiation");
#endif
