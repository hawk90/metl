# ---------------------------------------------------------------------------
# Micro-benchmarks (opt-in via -DMETL_BUILD_BENCHMARKS=ON)
#
# Built against the dependency-free harness in metl_bench.hpp; see the comment
# at the top of that header for why not google/benchmark. Always compiled -O2
# regardless of CMAKE_BUILD_TYPE, because a benchmark of a debug build measures
# the debug build.
#
# Registered with ctest under the `bench` label and run with --quick, so CI can
# prove they still build and run without pretending a shared runner produces
# meaningful numbers. There is deliberately NO performance gate: a threshold on
# a noisy machine either fires spuriously or is set so loose it never fires.
# Run them by hand for real numbers.
# ---------------------------------------------------------------------------

set(_metl_benchmarks
  bench_containers
  bench_mpmc
  bench_pools
  bench_spsc
)

foreach(_b IN LISTS _metl_benchmarks)
  set(_deps metl::metl)
  if(_b STREQUAL "bench_spsc" OR _b STREQUAL "bench_mpmc")
    list(APPEND _deps Threads::Threads)
  endif()

  metl_cc_benchmark(
    NAME metl_${_b}
    SRCS ${_b}.cpp
    DEPS ${_deps}
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
    COPTS -O2
  )

  add_test(NAME metl_${_b}_smoke COMMAND metl_${_b} --quick)
  set_tests_properties(metl_${_b}_smoke PROPERTIES LABELS "bench")
endforeach()
