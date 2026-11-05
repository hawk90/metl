// metl::version_* and METL_VERSION_* agree with project(VERSION) in CMakeLists.txt.
//
// The version is written in more than one place, and the release workflow only
// compared the tag with CMakeLists.txt -- so a release could ship a header whose
// metl::version_* still named the previous one. CMake passes its own numbers in
// here; the freestanding QEMU build has no such defines and only compiles.

#include "metl_check.hpp"

#include <metl/config.hpp>

#if defined(METL_EXPECTED_VERSION_MAJOR)
static_assert(metl::version_major == METL_EXPECTED_VERSION_MAJOR &&
                  METL_VERSION_MAJOR == METL_EXPECTED_VERSION_MAJOR,
              "METL_VERSION_MAJOR in config.hpp does not match project(VERSION) in CMakeLists.txt");
static_assert(metl::version_minor == METL_EXPECTED_VERSION_MINOR &&
                  METL_VERSION_MINOR == METL_EXPECTED_VERSION_MINOR,
              "METL_VERSION_MINOR in config.hpp does not match project(VERSION) in CMakeLists.txt");
static_assert(metl::version_patch == METL_EXPECTED_VERSION_PATCH &&
                  METL_VERSION_PATCH == METL_EXPECTED_VERSION_PATCH,
              "METL_VERSION_PATCH in config.hpp does not match project(VERSION) in CMakeLists.txt");
#endif

int main() {
  CHECK(metl::version_major >= 1);
  return metl_test::exit_code();
}
