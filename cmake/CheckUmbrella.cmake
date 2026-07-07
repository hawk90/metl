# CheckUmbrella.cmake
#
# Verifies that the umbrella header include/metl/metl.hpp includes every other
# public header under include/metl/ (recursively). Run as a standalone script:
#
#   cmake -D METL_INCLUDE_DIR=<path/to/include> -P cmake/CheckUmbrella.cmake
#
# Fails (non-zero) if any header is not referenced by metl.hpp, which guards the
# property that `#include <metl/metl.hpp>` pulls in the entire public API.

if(NOT DEFINED METL_INCLUDE_DIR)
  message(FATAL_ERROR "CheckUmbrella: METL_INCLUDE_DIR must be defined")
endif()

set(_umbrella "${METL_INCLUDE_DIR}/metl/metl.hpp")
if(NOT EXISTS "${_umbrella}")
  message(FATAL_ERROR "CheckUmbrella: umbrella header not found: ${_umbrella}")
endif()

file(READ "${_umbrella}" _umbrella_contents)

file(GLOB_RECURSE _headers
     RELATIVE "${METL_INCLUDE_DIR}"
     "${METL_INCLUDE_DIR}/metl/*.hpp")

set(_missing "")
foreach(_h IN LISTS _headers)
  if(_h STREQUAL "metl/metl.hpp")
    continue()
  endif()
  # The umbrella includes headers as `#include "metl/<relpath>"`; a substring
  # search for the relative path is sufficient and path-separator agnostic.
  string(FIND "${_umbrella_contents}" "${_h}" _pos)
  if(_pos EQUAL -1)
    list(APPEND _missing "${_h}")
  endif()
endforeach()

if(_missing)
  list(SORT _missing)
  string(REPLACE ";" "\n  " _pretty "${_missing}")
  message(FATAL_ERROR
    "metl.hpp umbrella is incomplete; missing includes for:\n  ${_pretty}")
endif()

list(LENGTH _headers _count)
message(STATUS "CheckUmbrella: metl.hpp includes all ${_count} public headers")
