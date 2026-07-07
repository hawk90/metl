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
  include/metl/attributes.hpp
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
  include/metl/detail/construct.hpp
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
  include/metl/optimization.hpp
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

  find_package(Threads REQUIRED)

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
    object_pool optional optional_constexpr
    register_access ring_buffer
    scope_exit span spsc_queue static_allocator static_message_queue
    static_unordered_map static_unordered_set
    type_traits
    variant
    # Focused correctness regression tests (Section A audit fixes).
    coro_scheduler_reentrancy
    static_unordered_map_emplace
    variant_regression
    expected_regression
    mmio_regression
    allocator_overflow
    fixed_function_const
    fsm_reentrancy
    hash_unique_repr
    intrusive_ptr_contract
    fixed_vector_asan
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

  # -------------------------------------------------------------------------
  # Multi-threaded concurrency test (needs a threading runtime). Built and run
  # in every configuration, including the TSAN sanitizer job where it actually
  # validates spsc_queue / atomic_ref memory ordering.
  # -------------------------------------------------------------------------
  metl_cc_test(
    NAME metl_spsc_queue_threaded_test
    SRCS tests/spsc_queue_threaded_test.cpp
    DEPS metl::metl_test_options Threads::Threads
    LABELS "metl"
  )

  # -------------------------------------------------------------------------
  # Header hygiene checks
  #   (a) self-containment: every public header compiles as its own TU
  #   (b) umbrella completeness: metl.hpp includes every other public header
  # Gated with the rest of the test surface (skipped e.g. on the freestanding
  # ARM cross build, whose curated freestanding check is embedded_smoke).
  # -------------------------------------------------------------------------
  option(METL_BUILD_HEADER_CHECKS
         "Build per-header self-containment + umbrella completeness checks" ON)
  if(METL_BUILD_HEADER_CHECKS)
    # (a) Generate one translation unit per public header, each including only
    # that header, and compile them. Building the target IS the check: a header
    # that is not self-contained fails to compile. CONFIGURE_DEPENDS re-globs so
    # newly added headers are picked up automatically.
    file(GLOB_RECURSE _metl_public_headers
         RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/include
         CONFIGURE_DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/include/metl/*.hpp)

    set(_selfcontain_srcs "")
    set(_selfcontain_dir ${CMAKE_CURRENT_BINARY_DIR}/header_checks)
    foreach(_h IN LISTS _metl_public_headers)
      string(MAKE_C_IDENTIFIER "${_h}" _id)
      set(_gen ${_selfcontain_dir}/selfcontain_${_id}.cpp)
      set(_content "#include <${_h}>\n")
      # Only (re)write when content changes, to avoid needless rebuilds.
      set(_existing "")
      if(EXISTS ${_gen})
        file(READ ${_gen} _existing)
      endif()
      if(NOT _existing STREQUAL _content)
        file(WRITE ${_gen} "${_content}")
      endif()
      list(APPEND _selfcontain_srcs ${_gen})
    endforeach()

    add_library(metl_header_self_contained OBJECT ${_selfcontain_srcs})
    target_link_libraries(metl_header_self_contained PRIVATE metl::metl)
    target_compile_features(metl_header_self_contained PRIVATE cxx_std_17)
    set_target_properties(metl_header_self_contained PROPERTIES METL_KIND header_check)

    # Register the self-containment build as a CTest test too, so `ctest` alone
    # (as CI runs it) fails if any header stops being self-contained.
    add_test(
      NAME metl_header_self_contained
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
              --target metl_header_self_contained --config $<CONFIG>)
    set_tests_properties(metl_header_self_contained PROPERTIES LABELS "metl")

    # (b) Umbrella completeness.
    add_test(
      NAME metl_umbrella_complete
      COMMAND ${CMAKE_COMMAND}
              -D METL_INCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckUmbrella.cmake)
    set_tests_properties(metl_umbrella_complete PROPERTIES LABELS "metl")
  endif()
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
  # Examples register themselves as CTest smoke tests. enable_testing() is
  # idempotent and safe even when METL_BUILD_TESTS already called it.
  enable_testing()
  add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Documentation  (Doxygen)
# ---------------------------------------------------------------------------
# `cmake --build build --target docs` generates HTML API docs into
# build/docs/html from the public headers. Optional: only wired up when Doxygen
# is found, so it never blocks a normal build. The CI `docs` job runs it with
# WARN_AS_ERROR so malformed doc comments fail.
option(METL_BUILD_DOCS "Add the Doxygen `docs` target when Doxygen is available" ON)
if(METL_BUILD_DOCS)
  find_package(Doxygen QUIET)
  if(DOXYGEN_FOUND)
    set(METL_DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs")
    # API reference is generated from the public headers only. Prose docs
    # (README / COOKBOOK) use GitHub-flavored intra-page anchor links that
    # Doxygen would (correctly) reject as unresolved \ref targets, so they are
    # intentionally not fed to Doxygen.
    set(METL_DOXYGEN_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/include/metl")
    set(METL_DOXYGEN_STRIP "${CMAKE_CURRENT_SOURCE_DIR}/include")
    set(_doxyfile_in "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in")
    set(_doxyfile "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
    configure_file(${_doxyfile_in} ${_doxyfile} @ONLY)

    add_custom_target(docs
      COMMAND ${CMAKE_COMMAND} -E make_directory ${METL_DOXYGEN_OUTPUT_DIR}
      COMMAND ${DOXYGEN_EXECUTABLE} ${_doxyfile}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating METL API documentation with Doxygen"
      VERBATIM)
  else()
    add_custom_target(docs
      COMMAND ${CMAKE_COMMAND} -E echo
              "Doxygen not found — install doxygen to build the API docs."
      COMMAND ${CMAKE_COMMAND} -E false
      COMMENT "Doxygen not available")
  endif()
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
