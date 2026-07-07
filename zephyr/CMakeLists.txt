# CMake hook for the metl Zephyr module.
#
# metl is header-only: there are no .c/.cpp sources to add to the RTOS image.
# When CONFIG_METL is enabled we simply add metl's public include/ directory to
# the Zephyr build so applications (and any Zephyr libraries) can
# `#include <metl/...>`.
#
# ZEPHYR_CURRENT_MODULE_DIR is set by Zephyr to this module's root (the parent
# of this zephyr/ directory), i.e. the metl repository root — so the headers
# live at ${ZEPHYR_CURRENT_MODULE_DIR}/include.

if(CONFIG_METL)
  zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)
endif()
