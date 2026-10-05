// metl::version agrees with project(VERSION) in CMakeLists.txt.
//
// The version is written in more than one place, and the release workflow only
// compared the tag with CMakeLists.txt -- so a release could ship a header whose
// metl::version still named the previous one. CMake passes its own numbers in
// here; the freestanding QEMU build has no such defines and only compiles.

#include "metl_check.hpp"

#include <metl/version.hpp>

#if defined(METL_EXPECTED_VERSION_MAJOR)
static_assert(metl::version::major == METL_EXPECTED_VERSION_MAJOR,
              "metl::version_major in config.hpp does not match project(VERSION) in CMakeLists.txt");
static_assert(metl::version::minor == METL_EXPECTED_VERSION_MINOR,
              "metl::version_minor in config.hpp does not match project(VERSION) in CMakeLists.txt");
static_assert(metl::version::patch == METL_EXPECTED_VERSION_PATCH,
              "metl::version_patch in config.hpp does not match project(VERSION) in CMakeLists.txt");
#endif

int main() {
  CHECK(metl::version::major >= 1);
  return metl_test::exit_code();
}
