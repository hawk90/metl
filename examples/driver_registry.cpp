// driver_registry.cpp
//
// Drivers that register themselves: each driver lives in its own translation
// unit (driver_registry/uart.cpp, driver_registry/spi.cpp), and linking the file
// in is the whole of registering it. Nothing here names a driver, and no table
// lists them all, with metl::intrusive_forward_list.
//
// Why an intrusive list rather than a fixed_vector<const driver*, N>:
//   * No capacity. The number of drivers is decided by what is linked in, which
//     no header can know; a vector needs an N that every build must outgrow.
//   * No initialization order to lose. The list is constant-initialized, so it
//     is a valid empty list before any registrar runs. A fixed_vector is only
//     constant-initialized since C++20; under C++17 on GCC a registrar can run
//     before the vector's own initialization and see its push undone.
//
// The one rule the pattern adds is the registrar's destructor: see
// driver_registry/registry.hpp. Remove it and this program stops at exit, in
// the hook's destructor, with the link order in examples/CMakeLists.txt.
//
// The program self-checks: every driver linked in is registered exactly once,
// in whichever order the translation units ran, and returns non-zero if not.

#include "driver_registry/registry.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include <metl/attributes.hpp>

namespace registry {

METL_CONST_INIT driver_list g_drivers;

}  // namespace registry

namespace {

struct expected_driver {
  const char* name;
  int probe;
};

// Drivers this program links in (see examples/CMakeLists.txt) and the answer
// each one's probe gives.
constexpr expected_driver kExpected[] = {{"uart", 0x0A}, {"spi", 0x5B}};
constexpr std::size_t kExpectedCount = sizeof(kExpected) / sizeof(kExpected[0]);

}  // namespace

int main() {
  // The order of registration is the order the translation units' initializers
  // ran in, which the program does not choose. Check membership, not order.
  if (registry::g_drivers.size() != kExpectedCount) {
    std::printf("registered %zu drivers, expected %zu\n", registry::g_drivers.size(), kExpectedCount);
    return 1;
  }

  std::size_t seen[kExpectedCount] = {};
  for (const registry::driver& d : registry::g_drivers) {
    std::size_t i = 0;
    while (i < kExpectedCount && std::strcmp(kExpected[i].name, d.name) != 0) {
      ++i;
    }
    if (i == kExpectedCount) {
      std::printf("unexpected driver '%s'\n", d.name);
      return 2;
    }
    const int answer = d.probe();
    if (answer != kExpected[i].probe) {
      std::printf("driver '%s' probed 0x%X, expected 0x%X\n", d.name, answer, kExpected[i].probe);
      return 3;
    }
    ++seen[i];
    std::printf("driver %-4s probe 0x%02X\n", d.name, answer);
  }

  for (std::size_t i = 0; i < kExpectedCount; ++i) {
    if (seen[i] != 1) {
      std::printf("driver '%s' seen %zu times, expected once\n", kExpected[i].name, seen[i]);
      return 4;
    }
  }

  std::printf("driver_registry: %zu drivers registered from %zu translation units\n",
              registry::g_drivers.size(),
              kExpectedCount);
  // Returning runs the destructors at exit. That is the rest of the check: a
  // driver destroyed while linked would stop the program here.
  return 0;
}
