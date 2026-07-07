# The `main` component for the metl ESP-IDF sample.
#
# Registers the single C++ source and declares a dependency on the header-only
# `metl` component. `REQUIRES metl` is what propagates metl's public include
# directory (registered in components/metl/CMakeLists.txt) onto this component's
# header search path, so main.cpp can `#include <metl/...>`.
idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES metl)
