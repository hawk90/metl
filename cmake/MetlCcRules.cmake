# MetlCcRules.cmake
#
# Bazel-style build helpers for METL. Each function mirrors a Bazel rule:
#   metl_cc_library   — INTERFACE (header-only) or STATIC library
#   metl_cc_test      — executable + ctest registration
#   metl_cc_binary    — executable (no test registration; for examples)
#   metl_cc_benchmark — executable wired against the benchmark harness
#
# Keyword arguments use cmake_parse_arguments. They are deliberately close to
# Bazel rule attributes so users can transfer mental models:
#
#   metl_cc_library(
#     NAME    foo
#     HDRS    include/metl/foo.hpp
#     SRCS    src/foo.cpp                 # omitted ⇒ INTERFACE (header-only)
#     DEPS    metl::core other::lib
#     COPTS   -Wno-shadow
#     DEFINES METL_FOO_FEATURE=1
#     PUBLIC                              # exposes deps/defines to consumers
#   )
#
#   metl_cc_test(
#     NAME  foo_test
#     SRCS  tests/foo_test.cpp
#     DEPS  metl::foo metl::test_options
#   )

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# metl_cc_library
# ---------------------------------------------------------------------------
function(metl_cc_library)
  set(options PUBLIC)
  set(one_value NAME)
  set(multi_value HDRS SRCS DEPS COPTS DEFINES INCLUDES)
  cmake_parse_arguments(ARG "${options}" "${one_value}" "${multi_value}" ${ARGN})

  if(NOT ARG_NAME)
    message(FATAL_ERROR "metl_cc_library: NAME is required")
  endif()

  if(ARG_SRCS)
    add_library(${ARG_NAME} STATIC ${ARG_SRCS} ${ARG_HDRS})
    set(scope PUBLIC)
  else()
    add_library(${ARG_NAME} INTERFACE)
    set(scope INTERFACE)
    if(ARG_HDRS)
      # Header-only library: expose headers via target_sources so IDEs/clangd
      # pick them up, but only at build time (not install time). Paths are
      # made absolute because target_sources requires absolute paths for
      # INTERFACE_SOURCES (otherwise transitive consumers in different
      # source dirs fail to resolve them).
      set(_abs_hdrs "")
      foreach(_h IN LISTS ARG_HDRS)
        if(IS_ABSOLUTE "${_h}")
          list(APPEND _abs_hdrs "${_h}")
        else()
          list(APPEND _abs_hdrs "${CMAKE_CURRENT_SOURCE_DIR}/${_h}")
        endif()
      endforeach()
      target_sources(${ARG_NAME} INTERFACE
        "$<BUILD_INTERFACE:${_abs_hdrs}>")
    endif()
  endif()

  add_library(metl::${ARG_NAME} ALIAS ${ARG_NAME})

  if(ARG_INCLUDES)
    target_include_directories(${ARG_NAME} ${scope} ${ARG_INCLUDES})
  endif()

  target_compile_features(${ARG_NAME} ${scope} cxx_std_17)

  if(ARG_DEPS)
    target_link_libraries(${ARG_NAME} ${scope} ${ARG_DEPS})
  endif()

  if(ARG_COPTS)
    target_compile_options(${ARG_NAME} ${scope} ${ARG_COPTS})
  endif()

  if(ARG_DEFINES)
    target_compile_definitions(${ARG_NAME} ${scope} ${ARG_DEFINES})
  endif()

  # Tag library so install rules can sweep them.
  set_target_properties(${ARG_NAME} PROPERTIES METL_KIND library)
endfunction()

# ---------------------------------------------------------------------------
# metl_cc_test
# ---------------------------------------------------------------------------
function(metl_cc_test)
  set(options "")
  set(one_value NAME TIMEOUT)
  set(multi_value SRCS DEPS COPTS DEFINES LABELS)
  cmake_parse_arguments(ARG "${options}" "${one_value}" "${multi_value}" ${ARGN})

  if(NOT ARG_NAME)
    message(FATAL_ERROR "metl_cc_test: NAME is required")
  endif()
  if(NOT ARG_SRCS)
    message(FATAL_ERROR "metl_cc_test(${ARG_NAME}): SRCS is required")
  endif()

  add_executable(${ARG_NAME} ${ARG_SRCS})

  if(ARG_DEPS)
    target_link_libraries(${ARG_NAME} PRIVATE ${ARG_DEPS})
  endif()
  if(ARG_COPTS)
    target_compile_options(${ARG_NAME} PRIVATE ${ARG_COPTS})
  endif()
  if(ARG_DEFINES)
    target_compile_definitions(${ARG_NAME} PRIVATE ${ARG_DEFINES})
  endif()

  add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME})

  if(ARG_TIMEOUT)
    set_tests_properties(${ARG_NAME} PROPERTIES TIMEOUT ${ARG_TIMEOUT})
  endif()
  if(ARG_LABELS)
    set_tests_properties(${ARG_NAME} PROPERTIES LABELS "${ARG_LABELS}")
  endif()

  set_target_properties(${ARG_NAME} PROPERTIES METL_KIND test)
endfunction()

# ---------------------------------------------------------------------------
# metl_cc_binary  (executable without ctest registration; for examples)
# ---------------------------------------------------------------------------
function(metl_cc_binary)
  set(options "")
  set(one_value NAME)
  set(multi_value SRCS DEPS COPTS DEFINES)
  cmake_parse_arguments(ARG "${options}" "${one_value}" "${multi_value}" ${ARGN})

  if(NOT ARG_NAME)
    message(FATAL_ERROR "metl_cc_binary: NAME is required")
  endif()
  if(NOT ARG_SRCS)
    message(FATAL_ERROR "metl_cc_binary(${ARG_NAME}): SRCS is required")
  endif()

  add_executable(${ARG_NAME} ${ARG_SRCS})

  if(ARG_DEPS)
    target_link_libraries(${ARG_NAME} PRIVATE ${ARG_DEPS})
  endif()
  if(ARG_COPTS)
    target_compile_options(${ARG_NAME} PRIVATE ${ARG_COPTS})
  endif()
  if(ARG_DEFINES)
    target_compile_definitions(${ARG_NAME} PRIVATE ${ARG_DEFINES})
  endif()

  set_target_properties(${ARG_NAME} PROPERTIES METL_KIND binary)
endfunction()

# ---------------------------------------------------------------------------
# metl_cc_benchmark  (placeholder — wires into google/benchmark when present)
# ---------------------------------------------------------------------------
function(metl_cc_benchmark)
  set(options "")
  set(one_value NAME)
  set(multi_value SRCS DEPS COPTS DEFINES)
  cmake_parse_arguments(ARG "${options}" "${one_value}" "${multi_value}" ${ARGN})

  if(NOT TARGET benchmark::benchmark)
    message(STATUS "metl_cc_benchmark(${ARG_NAME}) skipped: benchmark::benchmark not found")
    return()
  endif()

  add_executable(${ARG_NAME} ${ARG_SRCS})
  target_link_libraries(${ARG_NAME} PRIVATE benchmark::benchmark ${ARG_DEPS})

  if(ARG_COPTS)
    target_compile_options(${ARG_NAME} PRIVATE ${ARG_COPTS})
  endif()
  if(ARG_DEFINES)
    target_compile_definitions(${ARG_NAME} PRIVATE ${ARG_DEFINES})
  endif()

  set_target_properties(${ARG_NAME} PROPERTIES METL_KIND benchmark)
endfunction()
