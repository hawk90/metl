#pragma once

// Tiny dependency-free micro-benchmark harness, the counterpart to
// tests/metl_check.hpp.
//
// Why not google/benchmark, which docs/TODO.md originally named: it would be the
// first external dependency in a repo that has none, and pulling it in — even
// opt-in — costs CI a network fetch and a framework build. This library already
// made the same call once, choosing a hand-rolled metl_check.hpp over gtest. The
// methodology that actually matters here is small enough to own:
//
//   * auto-tune the iteration count until a run is long enough to time
//   * repeat, and report the MEDIAN rather than the mean, so one descheduled
//     run does not move the number
//   * report min and max alongside it, so noise is visible instead of hidden
//     behind a single figure
//   * defeat the optimizer explicitly rather than hoping the work survives -O2
//
// A benchmark that reports one number from one run is worse than no benchmark:
// it invites conclusions the measurement cannot support. Everything here exists
// to make the spread part of the output.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace metl_bench {

/// Prevents the optimizer from discarding a computed value.
template <typename T>
inline void do_not_optimize(const T& value) {
#if defined(_MSC_VER)
  // MSVC has no inline asm on x64; a volatile read of the bytes is the portable
  // stand-in and is enough to keep the value alive.
  volatile const char* sink = reinterpret_cast<volatile const char*>(&value);
  (void)*sink;
#else
  __asm__ __volatile__("" : : "r,m"(value) : "memory");
#endif
}

/// Prevents the optimizer from sinking or reordering stores across this point.
inline void clobber_memory() {
#if defined(_MSC_VER)
  _ReadWriteBarrier();
#else
  __asm__ __volatile__("" : : : "memory");
#endif
}

/// How long a single timed run should take, and how many runs to take. `--quick`
/// shrinks both so CI can prove the benchmarks still build and run without
/// pretending the numbers mean anything on a shared runner.
struct config {
  double target_seconds = 0.05;
  int repetitions = 7;

  static config from_args(int argc, char** argv) {
    config cfg;
    for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], "--quick") == 0) {
        cfg.target_seconds = 0.002;
        cfg.repetitions = 3;
      }
    }
    return cfg;
  }
};

namespace detail {

inline double median_of(std::vector<double> samples) {
  std::sort(samples.begin(), samples.end());
  const std::size_t n = samples.size();
  if (n == 0) {
    return 0.0;
  }
  return (n % 2 == 1) ? samples[n / 2] : 0.5 * (samples[n / 2 - 1] + samples[n / 2]);
}

inline void report(const char* name, const char* unit, double median, double min, double max) {
  // The spread is printed on every line on purpose: a 2x median improvement
  // means nothing if min and max straddle it.
  std::printf("%-40s %10.2f %-8s  [min %8.2f, max %8.2f]\n", name, median, unit, min, max);
}

}  // namespace detail

/// Times `body(iterations)`, auto-tuning the iteration count, and reports
/// nanoseconds per operation.
///
/// @param body Callable taking the iteration count; must perform exactly that
///        many operations and keep their results alive (`do_not_optimize`).
template <typename Body>
void run(const char* name, const config& cfg, Body&& body) {
  using clock = std::chrono::steady_clock;

  // Auto-tune: grow the iteration count until one run reaches the target.
  std::uint64_t iterations = 1024;
  for (;;) {
    const auto start = clock::now();
    body(iterations);
    const double seconds = std::chrono::duration<double>(clock::now() - start).count();
    if (seconds >= cfg.target_seconds || iterations >= (std::uint64_t{1} << 32)) {
      break;
    }
    // Jump straight to the estimate rather than doubling blindly, but never
    // shrink and never grow more than 100x in one step.
    const double growth = (seconds > 0.0) ? (cfg.target_seconds / seconds) : 100.0;
    const double factor = std::min(100.0, std::max(2.0, growth));
    iterations = static_cast<std::uint64_t>(static_cast<double>(iterations) * factor);
  }

  std::vector<double> per_op;
  per_op.reserve(static_cast<std::size_t>(cfg.repetitions));
  for (int r = 0; r < cfg.repetitions; ++r) {
    const auto start = clock::now();
    body(iterations);
    const double nanos = std::chrono::duration<double, std::nano>(clock::now() - start).count();
    per_op.push_back(nanos / static_cast<double>(iterations));
  }

  const double min = *std::min_element(per_op.begin(), per_op.end());
  const double max = *std::max_element(per_op.begin(), per_op.end());
  detail::report(name, "ns/op", detail::median_of(per_op), min, max);
}

/// Times a whole scenario that reports how many operations it performed, and
/// reports millions of operations per second. For work whose iteration count is
/// not the harness's to choose — a two-thread throughput run, for instance.
///
/// @param scenario Callable returning the number of operations it performed.
template <typename Scenario>
void run_scenario(const char* name, const config& cfg, Scenario&& scenario) {
  using clock = std::chrono::steady_clock;

  std::vector<double> mops;
  mops.reserve(static_cast<std::size_t>(cfg.repetitions));
  for (int r = 0; r < cfg.repetitions; ++r) {
    const auto start = clock::now();
    const std::uint64_t operations = scenario();
    const double seconds = std::chrono::duration<double>(clock::now() - start).count();
    mops.push_back(seconds > 0.0 ? (static_cast<double>(operations) / seconds / 1e6) : 0.0);
  }

  const double min = *std::min_element(mops.begin(), mops.end());
  const double max = *std::max_element(mops.begin(), mops.end());
  detail::report(name, "Mops/s", detail::median_of(mops), min, max);
}

/// Prints the column header once, before the first result.
inline void header(const char* suite) {
  std::printf("\n== %s ==\n", suite);
  std::printf("%-40s %10s %-8s  %s\n", "benchmark", "median", "", "spread over repetitions");
}

}  // namespace metl_bench
