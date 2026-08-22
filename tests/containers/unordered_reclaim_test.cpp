// Holds the in-place tombstone reclaim to what the headers claim about it.
//
// `static_unordered_map` and `static_unordered_set` erase by leaving a
// tombstone, because clearing the slot would break the probe chain running
// through it. A tombstone does not end a negative lookup -- only an empty slot
// does -- so without something to clear them out, misses get steadily more
// expensive under churn. `reclaim_if_needed` is that something: past one eighth
// of the table, `rehash_in_place` rebuilds it.
//
// WHY THIS TEST EXISTS. Replacing that trigger with `if (false)` in both headers
// leaves the entire rest of the suite green and every fuzz harness clean --
// measured, not assumed. It has to, because disabling the reclaim keeps the type
// CORRECT. Lookups still find what is there, the probe loop still counts to
// `bucket_count` and stops, and nothing observable to a correctness check
// changes. Only the typical cost degrades, and a fuzzer cannot see cost. So the
// reclaim could have been deleted by a refactor and no gate in this repository
// would have said a word.
//
// HOW IT IS OBSERVED, without adding API for it. `erase` moves nothing -- it
// destroys one element in place -- EXCEPT when it triggers a rebuild, which
// move-constructs every live element. So a key type that counts its own moves
// reports rebuilds exactly: moves during an erase is zero, or a rebuild fired.
// That is a count and not a duration, so there is nothing here to be flaky on a
// shared runner.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

/// Moves of any `counted_key`, anywhere. A single counter is enough because the
/// checks below read it immediately either side of one operation.
std::size_t g_moves = 0;

/// A key that reports being relocated. Deliberately NOT trivially copyable, so
/// the containers must actually move-construct it rather than memcpy the slot.
struct counted_key {
  std::uint32_t value;

  counted_key() noexcept : value(0) {}
  explicit counted_key(std::uint32_t v) noexcept : value(v) {}

  counted_key(const counted_key& other) noexcept : value(other.value) { ++g_moves; }
  counted_key(counted_key&& other) noexcept : value(other.value) { ++g_moves; }
  counted_key& operator=(const counted_key& other) noexcept {
    value = other.value;
    ++g_moves;
    return *this;
  }
  counted_key& operator=(counted_key&& other) noexcept {
    value = other.value;
    ++g_moves;
    return *this;
  }
  ~counted_key() = default;
};

struct counted_hash {
  std::size_t operator()(const counted_key& key) const noexcept {
    return static_cast<std::size_t>(key.value) * 2654435761u;
  }
};

struct counted_equal {
  bool operator()(const counted_key& a, const counted_key& b) const noexcept { return a.value == b.value; }
};

constexpr std::size_t kCapacity = 32;
constexpr std::uint32_t kLive = 20;  ///< comfortably under capacity, so inserts always fit
constexpr int kRounds = 20;          ///< enough churn to cross the threshold many times

/// Erase every live key one at a time, counting how many of those erases
/// relocated elements. Returns that count.
template <typename Container, typename Erase, typename Insert>
int churn(Container& container, Erase erase_one, Insert insert_one) {
  for (std::uint32_t k = 0; k < kLive; ++k) {
    insert_one(container, k);
  }

  int erases = 0;
  int erases_that_rebuilt = 0;

  for (int round = 0; round < kRounds; ++round) {
    for (std::uint32_t k = 0; k < kLive; ++k) {
      const std::size_t before = g_moves;
      if (erase_one(container, k)) {
        ++erases;
        if (g_moves > before) {
          ++erases_that_rebuilt;
        }
      }
    }
    for (std::uint32_t k = 0; k < kLive; ++k) {
      insert_one(container, k);
    }
  }

  // The churn itself has to have happened, or the counts below prove nothing.
  CHECK(erases > 0);
  return erases_that_rebuilt;
}

}  // namespace

int main() {
  // ---------------------------------------------------------------------
  // set: erasing under churn must, sometimes, rebuild.
  // ---------------------------------------------------------------------
  {
    metl::static_unordered_set<counted_key, kCapacity, counted_hash, counted_equal> set;
    const int rebuilds = churn(
        set,
        [](auto& s, std::uint32_t k) { return s.erase(counted_key{k}); },
        [](auto& s, std::uint32_t k) { (void)s.try_emplace(counted_key{k}); });

    // THE ASSERTION. Zero here means `reclaim_if_needed` never fired across
    // hundreds of erases that left far more than `bucket_count / 8` tombstones.
    // The set would still be correct; it would just have stopped bounding what
    // a miss costs, and the header's progress guarantee would be describing
    // code that no longer runs.
    CHECK(rebuilds > 0);

    // Correct as well as fast: everything reinserted is still reachable, which
    // is the property a rebuild could plausibly break.
    CHECK_EQ(set.size(), static_cast<std::size_t>(kLive));
    for (std::uint32_t k = 0; k < kLive; ++k) {
      CHECK(set.contains(counted_key{k}));
    }
  }

  // ---------------------------------------------------------------------
  // map: the same, through the other type's copy of the machinery.
  // ---------------------------------------------------------------------
  {
    metl::static_unordered_map<counted_key, std::uint32_t, kCapacity, counted_hash, counted_equal> map;
    const int rebuilds = churn(
        map,
        [](auto& m, std::uint32_t k) { return m.erase(counted_key{k}); },
        [](auto& m, std::uint32_t k) { (void)m.try_emplace(counted_key{k}, k); });

    CHECK(rebuilds > 0);

    CHECK_EQ(map.size(), static_cast<std::size_t>(kLive));
    for (std::uint32_t k = 0; k < kLive; ++k) {
      CHECK_DEREF_EQ(map.find(counted_key{k}), k);
    }
  }

  // ---------------------------------------------------------------------
  // The counter has to work, or every check above passes vacuously. A plain
  // insert relocates the key into its slot, so this is a positive control on
  // the instrument itself.
  // ---------------------------------------------------------------------
  {
    metl::static_unordered_set<counted_key, kCapacity, counted_hash, counted_equal> set;
    const std::size_t before = g_moves;
    CHECK(set.try_emplace(counted_key{1}));
    CHECK(g_moves > before);
  }

  return metl_test::exit_code();
}
