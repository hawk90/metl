// Counts the comparisons a lookup actually performs, and holds them to the
// bound the header promises.
//
// WHY. flat_map's and flat_set's progress-guarantee tables state an exact
// figure -- "wait-free, log2(Capacity) comparisons" -- and nothing measured it.
// A container whose binary search was replaced by a linear scan would keep
// every other property this repository checks: same results, same sizes, same
// `.text` budget within tolerance, same everything the fuzz harnesses look at.
// The cost would be visible only in a number no test computed.
//
// This is the complement to tools/check_instructions.py, not a duplicate of it.
// That gate counts x86 instructions on the CI host, so it sees the whole cost
// including code generation, and it cannot run on the target. This one counts
// COMPARISONS, which is a property of the algorithm rather than of any
// compiler -- so it is the same number on Cortex-M0 as on the runner, and it
// runs everywhere the test suite runs, QEMU included.
//
// WHAT IT DOES NOT COVER: static_unordered_map's tombstone reclamation (#18).
// A counting KeyEqual sees only the OCCUPIED slots a probe touches; tombstones
// are skipped on a state check with no comparison at all, so the cost that
// `rehash_in_place` exists to bound is invisible from here by construction.
// tools/check_instructions.py measures that one.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>

namespace {

std::size_t g_comparisons = 0;

struct counting_less {
  bool operator()(std::uint32_t a, std::uint32_t b) const noexcept {
    ++g_comparisons;
    return a < b;
  }
};

constexpr std::size_t floor_log2(std::size_t value) noexcept {
  std::size_t result = 0;
  while (value > 1) {
    value >>= 1;
    ++result;
  }
  return result;
}

// The measured worst case is floor(log2(size)) + 2, and the two extra come from
// a place worth naming: `lower_bound` over n elements performs floor(log2(n))+1
// comparisons, and `find` then performs one more to decide whether what
// lower_bound landed on is actually the key. The header used to say
// "log2(Capacity)", which understated it -- corrected in the same commit as
// this file.
constexpr std::size_t lookup_bound(std::size_t capacity) noexcept {
  return floor_log2(capacity) + 2;
}

// A binary search over 256 elements is ~10 comparisons; a linear scan is ~256.
// Anything at or above this is not a binary search any more, whatever it is
// called. Stated separately from the bound so a failure says WHICH claim broke.
//
// Only meaningful once the two are far apart: at Capacity 16 the bound is 6 and
// a quarter of the capacity is 4, so the test would fail on a perfectly correct
// binary search. Applied from 64 up, where the gap is at least 2x.
constexpr std::size_t linear_scan_smells_like(std::size_t capacity) noexcept {
  return capacity / 4;
}

constexpr bool linear_scan_check_is_meaningful(std::size_t capacity) noexcept {
  return capacity >= 64;
}

template <std::size_t Capacity>
void check_flat_map_lookup_bound() {
  metl::flat_map<std::uint32_t, std::uint32_t, Capacity, counting_less> map;
  for (std::uint32_t i = 0; i < Capacity; ++i) {
    CHECK(map.try_emplace(100u + (i * 2u), i));
  }
  CHECK_EQ(map.size(), Capacity);

  std::size_t worst = 0;
  std::size_t total = 0;
  // Every key from below the smallest to above the largest, so misses at both
  // ends are covered as well as hits and interior misses.
  for (std::uint32_t key = 0; key < 100u + (Capacity * 2u) + 4u; ++key) {
    g_comparisons = 0;
    (void)map.find(key);
    total += g_comparisons;
    if (g_comparisons > worst) {
      worst = g_comparisons;
    }
  }

  CHECK(worst <= lookup_bound(Capacity));
  if constexpr (linear_scan_check_is_meaningful(Capacity)) {
    CHECK(worst < linear_scan_smells_like(Capacity));
  }

  // The measurement must be real. If a future overload bypassed `Compare` --
  // a transparent-comparator path, say -- every count would be zero and the
  // bound above would pass without measuring anything.
  CHECK(worst > 0);
  CHECK(total > 0);
}

template <std::size_t Capacity>
void check_flat_set_lookup_bound() {
  metl::flat_set<std::uint32_t, Capacity, counting_less> set;
  for (std::uint32_t i = 0; i < Capacity; ++i) {
    CHECK(set.try_emplace(100u + (i * 2u)));
  }
  CHECK_EQ(set.size(), Capacity);

  std::size_t worst = 0;
  for (std::uint32_t key = 0; key < 100u + (Capacity * 2u) + 4u; ++key) {
    g_comparisons = 0;
    (void)set.find(key);
    if (g_comparisons > worst) {
      worst = g_comparisons;
    }
  }

  CHECK(worst <= lookup_bound(Capacity));
  if constexpr (linear_scan_check_is_meaningful(Capacity)) {
    CHECK(worst < linear_scan_smells_like(Capacity));
  }
  CHECK(worst > 0);
}

}  // namespace

int main() {
  // Powers of two and the values either side of one: the bound is stated in
  // terms of Capacity, and a binary search's behaviour at 255 / 256 / 257 is
  // where an off-by-one in the midpoint would show.
  check_flat_map_lookup_bound<16>();
  check_flat_map_lookup_bound<64>();
  check_flat_map_lookup_bound<255>();
  check_flat_map_lookup_bound<256>();
  check_flat_map_lookup_bound<257>();
  check_flat_map_lookup_bound<1024>();

  check_flat_set_lookup_bound<16>();
  check_flat_set_lookup_bound<256>();
  check_flat_set_lookup_bound<1024>();

  // A partly filled container costs less, not more: the bound is a function of
  // size(), and Capacity is only its worst case. Worth pinning because it is
  // the property that makes "log2(Capacity)" a safe thing for a caller to
  // budget against.
  {
    metl::flat_map<std::uint32_t, std::uint32_t, 1024, counting_less> map;
    for (std::uint32_t i = 0; i < 16; ++i) {
      CHECK(map.try_emplace(i * 2u, i));
    }
    std::size_t worst = 0;
    for (std::uint32_t key = 0; key < 40u; ++key) {
      g_comparisons = 0;
      (void)map.find(key);
      if (g_comparisons > worst) {
        worst = g_comparisons;
      }
    }
    CHECK(worst <= lookup_bound(16));
    CHECK(worst < lookup_bound(1024));
  }

  return metl_test::exit_code();
}
