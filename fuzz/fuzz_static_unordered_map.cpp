// libFuzzer harness for metl::static_unordered_map, checked against a model.
//
// Drives a fixed-capacity open-addressing hash map (linear probing with
// tombstones) with an opcode stream of CONTRACT-VALID operations only:
// try_emplace / try_insert_or_assign / erase / find / contains / clear are all
// return-based and never assert. The asserting members (emplace /
// insert_or_assign / operator[] on a full map) are deliberately NOT called.
//
// WHAT CHANGED. The old version checked that a FRESHLY INSERTED key was
// findable with the value just stored, plus size/iteration consistency. That is
// more than the other harnesses had, and it still leaves the interesting half
// unchecked: whether every OTHER key survived the operation intact.
//
// That is not a hypothetical gap for this container. `erase` reclaims tombstones
// through `rehash_in_place` (#18), which rebuilds the whole table in place. If
// a rebuild dropped an unrelated key or moved a value under the wrong one, the
// old harness would only have noticed had that key happened to be looked up
// immediately after its own insertion. The oracle compares the entire table
// against std::map after every single operation, so a rebuild that disturbs
// anything at all fails on the next opcode.
//
// Iteration ORDER is not compared: an open-addressed table has no defined
// order, and asserting one would be testing the hash function rather than the
// container. Contents are compared in both directions instead, which with equal
// sizes is exact equality.

#include "fuzz_helpers.hpp"
#include "fuzz_model.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/static_unordered_map.hpp>

namespace {

constexpr std::size_t kCapacity = 32;
using key_type = std::uint32_t;
using mapped_type = std::uint32_t;
using map_type = metl::static_unordered_map<key_type, mapped_type, kCapacity>;

using oracle_type = metl_fuzz::map_oracle<map_type, key_type, mapped_type, false>;

// KEY SPACE, and why it is narrowed. The interesting states of this container --
// hash collisions, long probe runs, tombstones, and the in-place rebuild that
// clears them -- are all reached only when keys COLLIDE and when an erase names
// a key that is actually present. Drawing keys from the full 2^32 range against
// a 32-slot map makes that probability about 7.5e-9 per operation, so
// `rehash_in_place` (#18) needs nine successful erases in a row and was
// effectively unreachable. Folding the key into a small space makes every one of
// those paths ordinary. The full width is still read, so bytes line up with the
// corpus that already exists; only the interpretation changes.
constexpr key_type kKeySpace = 64;  // 2x capacity: dense enough that erases hit

// REACHING THE REBUILD. `reclaim_if_needed` fires at `tombstones_ > bucket_count
// / 8`, which for this capacity is nine tombstones standing at once -- and an
// insert reuses a tombstone, so it takes a RUN of erases with nothing in
// between. Under a uniform opcode stream that run has probability (1/7)^9 per
// position, and instrumenting the old harness confirmed the arithmetic: zero
// calls to rehash_in_place across 228,000 operations.
//
// So the whole subject of #18 was unreachable by fuzzing, and no amount of
// running it longer would have fixed that. `erase_run` performs a burst of
// erases in one opcode. It is a real usage pattern -- draining or pruning a
// table -- and it makes the rebuild an ordinary event rather than a lottery.
constexpr std::size_t kMaxEraseRun = 32;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);
  map_type map;
  oracle_type oracle(map);

  while (!in.empty()) {
    const key_type key = static_cast<key_type>(in.integer<key_type>() % kKeySpace);
    switch (in.byte() % 8u) {
      case 0: {  // try_emplace — false on duplicate key or full (no assert)
        oracle.try_emplace(key, in.integer<mapped_type>());
        break;
      }
      case 1: {  // try_insert_or_assign — false only when a NEW key cannot fit
        oracle.try_insert_or_assign(key, in.integer<mapped_type>());
        break;
      }
      case 2: {  // erase — false if absent; this is the rehash_in_place path
        oracle.erase(key);
        break;
      }
      case 3: {  // find / contains, and the VALUE behind them
        oracle.lookup(key);
        break;
      }
      case 4: {  // full iteration visits exactly the model's entries
        std::size_t seen = 0;
        std::uint64_t acc = 0;
        for (const auto& item : map) {
          const auto entry = oracle.model().find(item.key);
          metl_fuzz::require(entry != oracle.model().end());
          metl_fuzz::require(item.value == entry->second);
          acc += item.value;
          ++seen;
        }
        metl_fuzz::require(seen == map.size());
        std::uint64_t expected = 0;
        for (const auto& entry : oracle.model()) {
          expected += entry.second;
        }
        metl_fuzz::require(acc == expected);
        break;
      }
      case 5: {  // find_iterator round-trip, against the model rather than
                 // against find() -- two broken lookups can agree with each
                 // other, which is what the old check compared.
        const auto entry = oracle.model().find(key);
        auto it = map.find_iterator(key);
        metl_fuzz::require((it != map.end()) == (entry != oracle.model().end()));
        if (it != map.end()) {
          metl_fuzz::require(it->key == key);
          metl_fuzz::require(it->value == entry->second);
        }
        break;
      }
      case 6: {  // erase_run — consecutive erases, the only way to the rebuild
        const std::size_t run = 1u + (in.byte() % kMaxEraseRun);
        for (std::size_t i = 0; i < run; ++i) {
          oracle.erase(static_cast<key_type>((key + i) % kKeySpace));
        }
        break;
      }
      default: {  // clear
        oracle.clear();
        break;
      }
    }
    metl_fuzz::require(map.size() <= map.capacity());
  }

  oracle.verify();
  return 0;
}
