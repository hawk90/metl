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
//
// TWO MODES, and the second one is why this file changed.
//
// The default mode above measures TIME, and .github/workflows/ci.yml is right
// that a wall-clock threshold on a shared runner either fires spuriously or
// never fires. But that argument is about time, and it had quietly been taken
// to cover performance as a whole. Instruction counts are not time: given the
// same binary, `valgrind --tool=cachegrind` returns the same number on a loaded
// runner, an idle one, and a laptop.
//
// The obstacle was in this file. Auto-tuning picks the iteration count from how
// fast the machine is, so the amount of WORK a run performs is not a property of
// the source -- and an instruction count over it would drift with the runner it
// landed on. `--fixed N` replaces the tuner with an exact count, which makes the
// work a deterministic function of the source and the toolchain, the same
// property tools/check_size.py relies on for `.text`.
//
// See tools/check_instructions.py for the ratchet built on top of it.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

  /// Non-zero switches off wall-clock auto-tuning: every `run` performs exactly
  /// this many iterations, once. The point is not speed but determinism -- the
  /// work becomes a function of the source rather than of the machine, so an
  /// instruction count over it is comparable across runs and across runners.
  std::uint64_t fixed_iterations = 0;

  /// Run only the benchmark with exactly this name.
  ///
  /// Exact, not a substring: substring matching would let one filter select two
  /// benchmarks whose names share a prefix, and the counting tool would ratchet
  /// their sum while reporting it under a single name. Names come from `--list`,
  /// so there is nothing to type by hand.
  ///
  /// One benchmark per process is deliberate. Cachegrind reports per-process
  /// totals, and attributing them per function does not survive -O2 inlining:
  /// every `run` call in bench/ instantiates the SAME template, because the
  /// bodies all have type `void(&)(std::uint64_t)`, so an inlined body lands in
  /// a symbol it shares with its neighbours. One process per benchmark needs no
  /// symbol attribution at all.
  const char* only = nullptr;

  /// Print the benchmark names and exit. The counting tool derives its work list
  /// from this rather than keeping a second copy -- the same reason
  /// tools/check_docs.py rule D4 exists.
  bool list_only = false;

  /// Run no benchmark at all. The counting tool measures this process too, and
  /// subtracts it: process startup -- the dynamic loader, libc initialisation,
  /// static constructors -- is thousands of instructions that have nothing to do
  /// with the code under test, and it moves when the toolchain or the C library
  /// moves. Subtracting a measured baseline keeps the ratchet on METL rather
  /// than on the runner image.
  bool baseline = false;

  static config from_args(int argc, char** argv) {
    config cfg;
    for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], "--quick") == 0) {
        cfg.target_seconds = 0.002;
        cfg.repetitions = 3;
      } else if (std::strcmp(argv[i], "--list") == 0) {
        cfg.list_only = true;
      } else if (std::strcmp(argv[i], "--baseline") == 0) {
        cfg.baseline = true;
      } else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
        cfg.only = argv[++i];
      } else if (std::strcmp(argv[i], "--fixed") == 0 && i + 1 < argc) {
        cfg.fixed_iterations = std::strtoull(argv[++i], nullptr, 10);
        if (cfg.fixed_iterations == 0) {
          std::fprintf(stderr, "error: --fixed needs a positive iteration count\n");
          std::exit(2);
        }
      }
    }
    return cfg;
  }
};

/// How many benchmarks `--only` actually selected. A `--only` that matches
/// nothing would otherwise measure an empty process and report a plausible
/// small number, which is the failure mode where a mistyped filter looks like a
/// performance win. `require_selection()` turns it into an exit code.
inline int& selected_count() {
  static int count = 0;
  return count;
}

/// Call at the end of `main`. Returns the process exit status.
inline int require_selection(const config& cfg) {
  if (cfg.baseline) {
    return 0;
  }
  if (cfg.only != nullptr && selected_count() == 0) {
    std::fprintf(stderr, "error: --only '%s' matched no benchmark in this binary\n", cfg.only);
    return 2;
  }
  return 0;
}

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

  if (cfg.list_only) {
    std::printf("%s\n", name);
    return;
  }
  if (cfg.baseline) {
    return;
  }
  if (cfg.only != nullptr && std::strcmp(name, cfg.only) != 0) {
    return;
  }
  ++selected_count();

  // Deterministic mode: exactly the requested work, once, and no output. The
  // print is omitted on purpose -- the counting tool measures this process
  // against an empty baseline process, and a formatted line is real
  // instructions that the baseline would not have.
  if (cfg.fixed_iterations != 0) {
    body(cfg.fixed_iterations);
    return;
  }

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

  if (cfg.list_only) {
    // The marker is load-bearing: tools/check_instructions.py skips these, and
    // it has to learn that from the binary rather than from a list of its own.
    std::printf("%s\t[scenario]\n", name);
    return;
  }
  if (cfg.baseline) {
    return;
  }
  if (cfg.only != nullptr && std::strcmp(name, cfg.only) != 0) {
    return;
  }

  // A scenario runs threads and reports the operations THEY completed, so the
  // work done is decided by the scheduler, not by the source. There is no
  // iteration count to fix and no instruction count to ratchet. Failing loudly
  // beats silently counting one interleaving as if it were the number: a
  // benchmark that cannot be made deterministic must not look like one that was.
  if (cfg.fixed_iterations != 0) {
    std::fprintf(stderr,
                 "error: '%s' is a thread-timed scenario and has no deterministic "
                 "instruction count; --fixed cannot apply to it\n",
                 name);
    std::exit(2);
  }
  ++selected_count();

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
///
/// Silent in every non-timing mode. `--list` output is parsed by
/// tools/check_instructions.py, and under `--fixed` a printf is instructions the
/// baseline process does not execute -- decoration that would land in the
/// measurement.
inline void header(const config& cfg, const char* suite) {
  if (cfg.list_only || cfg.baseline || cfg.only != nullptr || cfg.fixed_iterations != 0) {
    return;
  }
  std::printf("\n== %s ==\n", suite);
  std::printf("%-40s %10s %-8s  %s\n", "benchmark", "median", "", "spread over repetitions");
}

}  // namespace metl_bench
