// EXPECT-ERROR: atomic_ref supports 1/2/4/8 byte types
//
// A width the target has no atomic instruction for does not fail to build in
// general -- it silently lowers to a libatomic call, or to a lock, or on a
// bare-metal toolchain with no libatomic it fails at LINK time in whatever
// image happens to reference it. Refusing the type here turns all three into
// one diagnostic at the point of use.

#include <cstdint>

#include <metl/atomic_ref.hpp>

namespace {

struct four_bytes {
  std::uint32_t value;
};

struct three_bytes {
  std::uint8_t a;
  std::uint8_t b;
  std::uint8_t c;
};

}  // namespace

void control(four_bytes& slot) {
  metl::atomic_ref<four_bytes> ref(slot);
  (void)ref;
}

#ifdef METL_COMPILE_FAIL
void offender(three_bytes& slot) {
  metl::atomic_ref<three_bytes> ref(slot);
  (void)ref;
}
#endif
