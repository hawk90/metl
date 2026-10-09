// EXPECT-ERROR: mmio_ptr requires a scalar register type
//
// A register overlay struct, trivially copyable and all, cannot be read or
// written through a volatile lvalue: its implicit copy constructor and
// assignment take `const T&`, not `const volatile T&`. mmio_ptr used to accept
// it on "trivially copyable" and then fail deep inside read_once/write_once.

#include <cstdint>

#include <metl/mmio.hpp>

namespace {

struct overlay {
  std::uint32_t value;
};

enum class mode : std::uint32_t { off, on };

}  // namespace

using control_ptr = metl::mmio_ptr<std::uint32_t>;
static_assert(sizeof(control_ptr) > 0, "control instantiation");
using control_enum_ptr = metl::mmio_ptr<mode>;
static_assert(sizeof(control_enum_ptr) > 0, "an enum is a scalar");

#ifdef METL_COMPILE_FAIL
using offender_ptr = metl::mmio_ptr<overlay>;
static_assert(sizeof(offender_ptr) > 0, "forces the instantiation");
#endif
