cmake_minimum_required(VERSION 3.16)

project(
  metl
  VERSION 0.1.0
  DESCRIPTION "Modern Embedded Template Library"
  HOMEPAGE_URL "https://github.com/hawk90/metl"
  LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MetlCcRules)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(METL_BUILD_TESTS "Build METL tests" ON)
option(METL_BUILD_EXAMPLES "Build METL examples" OFF)
option(METL_BUILD_EMBEDDED_SMOKE
       "Build the freestanding embedded smoke compile-check static library" OFF)
option(METL_ENABLE_ASAN "Enable AddressSanitizer for tests" OFF)
option(METL_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for tests" OFF)
option(METL_ENABLE_TSAN "Enable ThreadSanitizer for tests" OFF)
option(METL_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(METL_INSTALL "Generate install targets" ON)

# ---------------------------------------------------------------------------
# Public library  (umbrella header-only target)
# ---------------------------------------------------------------------------
set(_metl_headers
  include/metl/arena_allocator.hpp
  include/metl/assert.hpp
  include/metl/atomic_ref.hpp
  include/metl/bit.hpp
  include/metl/bitfield.hpp
  include/metl/compiler.hpp
  include/metl/config.hpp
  include/metl/coro/protothread.hpp
  include/metl/coro/scheduler.hpp
  include/metl/coro/stepper.hpp
  include/metl/crc16.hpp
  include/metl/crc32.hpp
  include/metl/crc8.hpp
  include/metl/delegate.hpp
  include/metl/endian.hpp
  include/metl/event_dispatcher.hpp
  include/metl/expected.hpp
  include/metl/fixed_deque.hpp
  include/metl/fixed_function.hpp
  include/metl/fixed_queue.hpp
  include/metl/fixed_stack.hpp
  include/metl/fixed_string.hpp
  include/metl/fixed_vector.hpp
  include/metl/flat_map.hpp
  include/metl/flat_set.hpp
  include/metl/fsm.hpp
  include/metl/function_ref.hpp
  include/metl/hash.hpp
  include/metl/in_place.hpp
  include/metl/intrusive_ptr.hpp
  include/metl/lookup_table.hpp
  include/metl/metl.hpp
  include/metl/mmio.hpp
  include/metl/monotonic_buffer.hpp
  include/metl/object_pool.hpp
  include/metl/optional.hpp
  include/metl/register_access.hpp
  include/metl/ring_buffer.hpp
  include/metl/scope_exit.hpp
  include/metl/span.hpp
  include/metl/spsc_queue.hpp
  include/metl/static_allocator.hpp
  include/metl/static_message_queue.hpp
  include/metl/static_unordered_map.hpp
  include/metl/static_unordered_set.hpp
  include/metl/type_traits.hpp
  include/metl/variant.hpp
  include/metl/version.hpp
)

metl_cc_library(
  NAME metl
  HDRS ${_metl_headers}
  INCLUDES
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# ---------------------------------------------------------------------------
# Test options  (warnings + sanitizers — only linked by tests)
# ---------------------------------------------------------------------------
if(METL_BUILD_TESTS)
  enable_testing()

  metl_cc_library(
    NAME metl_test_options
    DEPS metl::metl
  )

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(metl_test_options INTERFACE
      -Wall -Wextra -Wpedantic
      -Wshadow -Wnon-virtual-dtor -Wcast-align
      -Wnull-dereference -Wdouble-promotion -Wformat=2)
    if(METL_WARNINGS_AS_ERRORS)
      target_compile_options(metl_test_options INTERFACE -Werror)
    endif()
    if(METL_ENABLE_ASAN)
      target_compile_options(metl_test_options INTERFACE -fsanitize=address -fno-omit-frame-pointer)
      target_link_options(metl_test_options INTERFACE -fsanitize=address)
    endif()
    if(METL_ENABLE_UBSAN)
      target_compile_options(metl_test_options INTERFACE -fsanitize=undefined -fno-omit-frame-pointer)
      target_link_options(metl_test_options INTERFACE -fsanitize=undefined)
    endif()
    if(METL_ENABLE_TSAN)
      target_compile_options(metl_test_options INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
      target_link_options(metl_test_options INTERFACE -fsanitize=thread)
    endif()
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(metl_test_options INTERFACE /W4 /permissive-)
    if(METL_WARNINGS_AS_ERRORS)
      target_compile_options(metl_test_options INTERFACE /WX)
    endif()
  endif()

  # -------------------------------------------------------------------------
  # Test suite  (Bazel-style — one rule per test)
  # -------------------------------------------------------------------------
  set(_metl_tests
    smoke
    arena_allocator assert atomic_ref
    bit bitfield
    compiler
    coro_protothread coro_scheduler coro_stepper
    crc16 crc32 crc8
    delegate
    endian event_dispatcher expected
    fixed_deque fixed_function fixed_queue fixed_stack fixed_string fixed_vector
    flat_map flat_set fsm function_ref
    hash
    intrusive_ptr
    lookup_table
    mmio monotonic_buffer
    object_pool optional
    register_access ring_buffer
    scope_exit span spsc_queue static_allocator static_message_queue
    static_unordered_map static_unordered_set
    type_traits
    variant
  )

  foreach(_t IN LISTS _metl_tests)
    if(_t STREQUAL "smoke")
      set(_src tests/smoke.cpp)
    else()
      set(_src tests/${_t}_test.cpp)
    endif()
    metl_cc_test(
      NAME metl_${_t}_test
      SRCS ${_src}
      DEPS metl::metl_test_options
      LABELS "metl"
    )
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Embedded smoke (freestanding compile check)
# ---------------------------------------------------------------------------
# Builds tests/embedded_smoke.cpp as a STATIC library so the cross toolchain
# only verifies that every public header instantiates cleanly under freestanding
# ARM (Cortex-M). Intentionally not registered with ctest -- this is a
# compile-only check, never executed. Gated by its own option so it activates
# only with the ARM toolchain (or any other freestanding build the user opts
# into).
if(METL_BUILD_EMBEDDED_SMOKE)
  add_library(metl_embedded_smoke STATIC tests/embedded_smoke.cpp)
  target_link_libraries(metl_embedded_smoke PRIVATE metl::metl)
  target_compile_features(metl_embedded_smoke PRIVATE cxx_std_17)
  set_target_properties(metl_embedded_smoke PROPERTIES METL_KIND embedded_smoke)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(METL_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Install / Export
# ---------------------------------------------------------------------------
if(METL_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(DIRECTORY include/metl
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(TARGETS metl
          EXPORT metlTargets
          INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(EXPORT metlTargets
          FILE metlTargets.cmake
          NAMESPACE metl::
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/metl)

  set(_cfg_in ${CMAKE_CURRENT_SOURCE_DIR}/cmake/metlConfig.cmake.in)
  set(_cfg_out ${CMAKE_CURRENT_BINARY_DIR}/metlConfig.cmake)
  set(_ver_out ${CMAKE_CURRENT_BINARY_DIR}/metlConfigVersion.cmake)

  if(EXISTS ${_cfg_in})
    configure_package_config_file(${_cfg_in} ${_cfg_out}
      INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/metl)
    write_basic_package_version_file(${_ver_out}
      VERSION ${PROJECT_VERSION}
      COMPATIBILITY SameMajorVersion
      ARCH_INDEPENDENT)
    install(FILES ${_cfg_out} ${_ver_out}
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/metl)
  endif()
endif()
