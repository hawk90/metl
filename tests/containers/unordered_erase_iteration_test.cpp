// Regression test: erasing while iterating a static unordered container must
// visit every element.
//
// The tombstone reclaim used to run inside `erase`. Once it fired, it moved live
// elements to slots the iterator had already passed, so the std-safe loop
//
//     auto key = it->key; ++it; map.erase(key);
//
// skipped them -- 62 of 64 elements visited in the original repro. The reclaim
// now runs on insertion of a new key, as a rehash does in std::unordered_map,
// and erase moves nothing.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

constexpr std::size_t kCapacity = 64;
constexpr std::uint32_t kSeeds = 64;  // seed 1 skipped two elements before the fix

// Fills `keys` with kCapacity distinct pseudo-random keys for `seed` (an LCG),
// using the map itself to reject duplicates. Spread keys give long probe runs,
// so the erase pass crosses the reclaim threshold many times.
template <typename Map>
void fill(Map& map, std::uint32_t seed, int (&keys)[kCapacity]) {
  std::uint32_t x = seed * 2654435761U + 1U;
  std::size_t n = 0;
  while (n < kCapacity) {
    x = x * 1103515245U + 12345U;
    const int key = static_cast<int>(x >> 8U);
    if (map.try_emplace(key, key)) {
      keys[n++] = key;
    }
  }
}

bool contains_key(const int (&keys)[kCapacity], std::size_t count, int key) {
  for (std::size_t i = 0; i < count; ++i) {
    if (keys[i] == key) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  // Map: erase two of every three while iterating; every key must be visited
  // exactly once, for every seed.
  for (std::uint32_t seed = 0; seed < kSeeds; ++seed) {
    metl::static_unordered_map<int, int, kCapacity> map;
    int keys[kCapacity] = {};
    fill(map, seed, keys);

    int seen[kCapacity] = {};
    std::size_t visited = 0;
    bool repeated = false;
    for (auto it = map.begin(); it != map.end();) {
      const int key = it->key;
      ++it;
      if (visited < kCapacity) {
        repeated = repeated || contains_key(seen, visited, key);
        seen[visited] = key;
      }
      ++visited;
      if (key % 3 != 0) {
        CHECK(map.erase(key));
      }
    }
    CHECK_EQ(visited, kCapacity);
    CHECK(!repeated);
    if (visited != kCapacity || repeated) {
      return metl_test::exit_code();  // one seed's report is enough
    }
  }

  // Set: erase everything while iterating.
  {
    metl::static_unordered_set<std::uint32_t, kCapacity> set;
    std::uint32_t x = 2654435762U;
    while (set.size() < kCapacity) {
      x = x * 1103515245U + 12345U;
      (void)set.try_emplace(x >> 8U);
    }
    std::size_t visited = 0;
    for (auto it = set.begin(); it != set.end();) {
      const std::uint32_t key = *it;
      ++visited;
      ++it;
      CHECK(set.erase(key));
    }
    CHECK_EQ(visited, kCapacity);
    CHECK(set.empty());
  }

  // The tombstones left behind are reclaimed by the next inserts, and the
  // table still answers correctly afterwards.
  {
    metl::static_unordered_map<int, int, kCapacity> map;
    for (std::uint32_t round = 0; round < 8; ++round) {
      int keys[kCapacity] = {};
      fill(map, round, keys);
      for (auto it = map.begin(); it != map.end();) {
        const int key = it->key;
        ++it;
        CHECK(map.erase(key));
      }
      CHECK(map.empty());
    }
    CHECK(map.try_emplace(7, 70));
    CHECK(map.find(7) != nullptr);
    CHECK(map.find(8) == nullptr);
  }

  return metl_test::exit_code();
}
