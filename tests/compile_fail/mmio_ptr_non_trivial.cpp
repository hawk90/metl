// EXPECT-ERROR: mmio_ptr requires a trivially copyable type
//
// An MMIO access is a single load or store the compiler may not split or
// reorder. A type whose copy runs user code cannot be read that way, so the
// peripheral would see an arbitrary sequence of accesses -- and peripherals
// have side effects on read, so the extra accesses are not merely wasteful.

#include <cstdint>

#include <metl/mmio.hpp>

namespace {

struct plain {
  std::uint32_t value;
};

struct has_user_copy {
  std::uint32_t value;
  has_user_copy(const has_user_copy& other) : value(other.value) {}
};

}  // namespace

using control_ptr = metl::mmio_ptr<plain>;
static_assert(sizeof(control_ptr) > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using offender_ptr = metl::mmio_ptr<has_user_copy>;
static_assert(sizeof(offender_ptr) > 0, "forces the instantiation");
#endif
