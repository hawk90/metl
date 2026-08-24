// EXPECT-ERROR: mmio_register requires a trivially copyable type
//
// The register form of the mmio_ptr case, and a separate assertion in the same
// header: an access that runs user code is not the single load or store the
// peripheral's datasheet describes.

#include <cstdint>

#include <metl/mmio.hpp>

namespace {

struct plain {
  std::uint32_t value;
};

struct has_user_dtor {
  std::uint32_t value;
  ~has_user_dtor() {}
};

}  // namespace

using control_reg = metl::mmio_register<plain, 0x40000000>;
static_assert(control_reg::address != 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender_reg = metl::mmio_register<has_user_dtor, 0x40000000>;
static_assert(offender_reg::address != 0, "forces the instantiation");
#endif
