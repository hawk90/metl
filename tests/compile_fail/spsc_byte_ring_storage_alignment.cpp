// EXPECT-ERROR: spsc_byte_ring StorageAlignment must be a power of two

#include <metl/spsc_byte_ring.hpp>

#ifdef METL_COMPILE_FAIL
using ring = metl::spsc_byte_ring<16, 24>;
#else
using ring = metl::spsc_byte_ring<16, 32>;
#endif

int main() {
  ring r;
  return r.empty() ? 0 : 1;
}
