# ESP-IDF component registration for metl.
#
# metl is a header-only C++17 library: there are no .c/.cpp sources to compile
# into the firmware image, so this component registers *only* an include
# directory. Any IDF component (or the app's `main`) that declares
# `REQUIRES metl` (or `PRIV_REQUIRES metl`) then gets metl's public `include/`
# on its header search path and can `#include <metl/...>`.
#
# Layout follows the standard ESP-IDF "Build System / Components" convention:
#   <component-root>/CMakeLists.txt      (this file — calls idf_component_register)
#   <component-root>/idf_component.yml   (component-manager manifest)
# metl's headers live two levels up, at the repository root's include/ dir:
#   components/metl/CMakeLists.txt  ->  ../../include/metl/*.hpp
#
# INCLUDE_DIRS with no SRCS is the idiomatic way to register a header-only
# (INTERFACE-style) IDF component. See:
#   https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html
idf_component_register(INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../include")
