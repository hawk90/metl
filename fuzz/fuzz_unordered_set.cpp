// libFuzzer harness for metl::static_unordered_set.
//
// The sibling of fuzz_static_unordered_map, and the one with the most to find.
// Open addressing with linear probing has a failure mode that a size check
// cannot see: a key that is present but no longer reachable, because an erase
// broke the probe chain that led to it. That is invisible until somebody looks
// it up, which is why this harness looks EVERY stored key up after every
// operation rather than only the one it just touched.
//
// It also exercises the in-place tombstone reclaim (#18). Once tombstones pass
// an eighth of the table, `rehash_in_place` rebuilds it without allocating,
// moving live elements through a carry loop -- the `for (;;)` this library
// documents as bounded because each iteration turns one more slot permanently
// occupied. An insert/erase stream that keeps crossing that threshold is exactly
// what drives it, and driving it is the point: a reclaim that dropped or
// duplicated an element would leave the table consistent-looking and wrong.
//
// Contract-valid only: `try_emplace` and `erase` report by return value.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/static_unordered_set.hpp>

namespace {

// Deliberately small. A 16-slot table crosses the tombstone threshold after two
// erases, so a short fuzz input reaches the reclaim path instead of spending its
// whole budget filling a big table.
constexpr std::size_t kCapacity = 16;
using set_type = metl::static_unordered_set<std::uint16_t, kCapacity>;

/// Every key the set reports through iteration must also be findable through the
/// probe path. These are different code paths -- one walks slots, the other
/// follows a hash -- and an erase that broke a probe chain makes them disagree
/// while both still look internally consistent.
void check_invariants(const set_type& set) {
  if (set.size() > kCapacity) {
    __builtin_trap();
  }
  std::size_t seen = 0;
  for (const auto& key : set) {
    if (!set.contains(key)) {
      __builtin_trap();  // present in storage, unreachable by lookup
    }
    ++seen;
  }
  if (seen != set.size()) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);
  set_type set;

  while (!in.empty()) {
    // A small key space on purpose: collisions and repeated keys are what
    // exercise probing and tombstones, and a full 16-bit key space would make
    // both rare.
    const auto key = static_cast<std::uint16_t>(in.integer<std::uint16_t>() % 64u);

    switch (in.byte() % 5u) {
      case 0: {  // try_emplace
        const bool present_before = set.contains(key);
        const std::size_t size_before = set.size();
        const bool inserted = set.try_emplace(std::uint16_t{key});
        if (inserted) {
          if (present_before) {
            __builtin_trap();
          }
          if (set.size() != size_before + 1 || !set.contains(key)) {
            __builtin_trap();
          }
        } else {
          // Refused: either a duplicate, or the table is full. Either way the
          // set must be unchanged.
          if (set.size() != size_before) {
            __builtin_trap();
          }
          if (!present_before && set.size() < kCapacity) {
            __builtin_trap();  // refused a new key while there was room
          }
        }
        break;
      }
      case 1: {  // erase -- the operation that can break a probe chain
        const bool present_before = set.contains(key);
        const std::size_t size_before = set.size();
        const bool erased = set.erase(key);
        if (erased != present_before) {
          __builtin_trap();
        }
        if (set.size() != size_before - (erased ? 1u : 0u)) {
          __builtin_trap();
        }
        if (set.contains(key)) {
          __builtin_trap();
        }
        break;
      }
      case 2: {  // find / contains must agree
        if ((set.find(key) != nullptr) != set.contains(key)) {
          __builtin_trap();
        }
        break;
      }
      case 3: {  // fill to the brim, then drain -- the tombstone-heavy path
        for (std::uint16_t k = 0; k < 64 && set.size() < kCapacity; ++k) {
          (void)set.try_emplace(std::uint16_t{k});
        }
        if (set.size() != kCapacity) {
          __builtin_trap();  // could not fill a table that reports room
        }
        for (std::uint16_t k = 0; k < 64; k += 2) {
          (void)set.erase(k);
        }
        // Everything still present must still be reachable after that many
        // tombstones -- this is the reclaim path's actual job.
        check_invariants(set);
        break;
      }
      default: {
        set.clear();
        if (!set.empty() || set.size() != 0) {
          __builtin_trap();
        }
        break;
      }
    }
    check_invariants(set);
  }

  return 0;
}
