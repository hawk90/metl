// A deterministic driver for the fuzz harnesses, so they run as ordinary tests.
//
// WHY. The harnesses under fuzz/ hold the only reference models in the tree --
// #82 put a std::map oracle behind flat_map and static_unordered_map, and the
// set harnesses carry hand-rolled ones. Until now those oracles ran in exactly
// two places: the `fuzz-smoke` job (Ubuntu, clang, libFuzzer) and
// ClusterFuzzLite. They did not run on macOS, on gcc, on any cross target, or
// under QEMU, because libFuzzer was required to invoke them at all.
//
// Nothing about a harness needs libFuzzer. `LLVMFuzzerTestOneInput` takes bytes
// and returns; libFuzzer's contribution is *choosing* the bytes. Choosing them
// from a fixed seed instead gives up the coverage guidance and keeps the
// oracles, which is the half that finds wrong answers.
//
// This is NOT a replacement for fuzzing. Coverage-guided mutation reaches states
// a PRNG will not, and the corpus ClusterFuzzLite has accumulated is worth more
// than any seed. What this adds is that the oracles now run wherever ctest runs
// -- Linux, macOS and Windows, gcc, clang and MSVC, under the sanitizers, under
// LTO, through the amalgamation -- and that a mutation whose only killer is a
// harness can be caught by tools/check_mutants.py without libFuzzer on the
// machine.
//
// STATED LIMIT, because the first version of this comment overstated it: "every
// platform" does NOT include the cross targets. tools/run_qemu_tests.sh
// discovers `tests/**/*_test.cpp`, and these targets are not tests/, so no
// oracle runs on Cortex-M. That is the interesting half -- 32-bit size_type and
// a different ABI are where a container's index arithmetic would differ from
// the host -- and it is open, not covered. The obstacle is not the driver: the
// oracles are std::map, which allocates, and this file uses <random> and
// std::vector, so a freestanding build is real work rather than a glob change.
// docs/TODO.md carries it.
//
// Determinism is the point, so nothing here may consult the clock, the
// environment, or an unseeded generator: the same binary must fail on the same
// input every time, or a red run is not reproducible.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

// The seed is a deliberate constant: the same binary must fail on the same
// input every time, or a red run is not reproducible.
constexpr std::uint64_t kSeed = 0x9E3779B97F4A7C15ULL;

// Overridable at build time so ctest can run a cheaper sweep than a manual one.
#ifndef METL_REPLAY_ROUNDS
#define METL_REPLAY_ROUNDS 4000
#endif
constexpr std::size_t kDefaultRounds = METL_REPLAY_ROUNDS;

// Long enough to reach the states that need a run of operations -- the
// tombstone rebuild in static_unordered_map needs nine consecutive erases, and
// a short input cannot contain them.
constexpr std::size_t kMaxInput = 8192;

}  // namespace

int main(int argc, char** argv) {
  std::size_t rounds = kDefaultRounds;
  if (argc > 1) {
    rounds = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    if (rounds == 0) {
      std::fprintf(stderr, "usage: %s [rounds]\n", argv[0]);
      return 2;
    }
  }

  std::mt19937_64 rng(kSeed);
  std::vector<std::uint8_t> input;
  input.reserve(kMaxInput);

  for (std::size_t round = 0; round < rounds; ++round) {
    const std::size_t size = 1 + static_cast<std::size_t>(rng() % kMaxInput);
    input.resize(size);
    for (std::size_t i = 0; i < size; ++i) {
      input[i] = static_cast<std::uint8_t>(rng());
    }
    // A harness reports a violation by trapping, so reaching the next line at
    // all is the pass condition. The return value is unused by libFuzzer too.
    (void)LLVMFuzzerTestOneInput(input.data(), input.size());
  }

  std::printf("replayed %zu deterministic inputs, no trap\n", rounds);
  return 0;
}
