# examples/CMakeLists.txt
#
# Each example is a self-contained main() exercising one or more METL
# modules. Examples link only against metl::metl — they do not pull in
# the test warning/sanitizer options, but we still compile them with
# -Wall -Wextra -Werror below so a warning in an example fails the build.
#
# Every example returns 0 on success and a non-zero code on a broken
# invariant, so each one is also registered with CTest and actually run.

# One category-focused example per module family, plus the pre-existing
# larger "realistic" sketches (sensor_pipeline, can_frame_parser).
set(_metl_examples
  containers        # fixed_vector + flat_map + ring_buffer
  allocators        # arena_allocator + monotonic_buffer
  spsc_isr          # spsc_queue ISR<->main-loop pattern
  mmio_peripheral   # mmio + register_access + bitfield (fake peripheral)
  error_handling    # expected + optional + variant (no exceptions)
  coroutine_task    # coro/protothread cooperative tasks
  blinky_fsm        # fsm + mmio + delegate
  can_frame_parser  # bitfield + crc16 + span + expected
  sensor_pipeline   # realistic: spsc_queue -> ring_buffer -> fixed_vector
)

foreach(_ex IN LISTS _metl_examples)
  metl_cc_binary(
    NAME metl_example_${_ex}
    SRCS ${_ex}.cpp
    DEPS metl::metl
  )

  # Examples are advertised as compiling clean under the strict flag set, so
  # build them that way (GNU/Clang; MSVC gets the /W4 equivalent).
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(metl_example_${_ex} PRIVATE -Wall -Wextra -Werror)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(metl_example_${_ex} PRIVATE /W4 /WX)
  endif()

  # Run each example as a smoke test (they self-check and return non-zero on
  # failure), so `ctest` catches an example that stops behaving correctly.
  add_test(NAME metl_example_${_ex} COMMAND metl_example_${_ex})
  set_tests_properties(metl_example_${_ex} PROPERTIES LABELS "metl;example")
endforeach()

# Threaded example needs the system thread library on POSIX.
find_package(Threads QUIET)
if(TARGET Threads::Threads)
  target_link_libraries(metl_example_sensor_pipeline PRIVATE Threads::Threads)
endif()
