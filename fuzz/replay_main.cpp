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
// than any seed. What this adds is that the oracles now run everywhere the test
// suite runs, on every platform and every toolchain, and that a mutation whose
// only killer is a harness can be caught by tools/check_mutants.py without
// libFuzzer on the machine.
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
