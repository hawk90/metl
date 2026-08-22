// libFuzzer harness for metl::flat_set.
//
// The sibling of fuzz_flat_map, and worth its own harness rather than trusting
// the map's: the two types keep the same sorted-array invariant through
// SEPARATE copies of the shifting and binary-search code (see docs/SCOPE.md on
// why they were not deduplicated), so a bug in one is not a bug in the other.
//
// Only contract-valid operations: `try_emplace` and `erase` report by return
// value and never assert, `find`/`contains` are total, and positional access is
// bounded by `% size()`. `try_insert_at` is NOT called -- it takes an index the
// caller promises is the sorted position, and feeding it a fuzzed index would
// break the sorted invariant on purpose rather than find a defect.
//
// After every op the invariants are checked, so a shift that drops or duplicates
// an element shows up immediately instead of as a wrong answer three operations
// later.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/flat_set.hpp>

namespace {

constexpr std::size_t kCapacity = 32;
using set_type = metl::flat_set<std::uint16_t, kCapacity>;

void check_invariants(const set_type& set) {
  if (set.size() > set.capacity()) {
    __builtin_trap();
  }
  // Strictly ascending: sorted AND unique, both of which the type promises.
  for (std::size_t i = 1; i < set.size(); ++i) {
    if (!(set.nth(i - 1) < set.nth(i))) {
      __builtin_trap();
    }
  }
  // Every stored key must be findable. A binary search that disagrees with the
  // storage order is the failure this catches, and it is invisible to a size or
  // ordering check.
  for (std::size_t i = 0; i < set.size(); ++i) {
    if (!set.contains(set.nth(i))) {
      __builtin_trap();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);
  set_type set;

  while (!in.empty()) {
    // A SMALL key space, and this is not a detail. Drawing keys from the full
    // 16-bit range against a 32-element set makes `erase(key)` miss essentially
    // always, so the shifting code -- the part most likely to be wrong -- is
    // barely reached. Mutation-testing caught exactly that: with full-width
    // keys this harness did not notice an `erase_at` that shifted one element
    // too few. With the range narrowed it kills that mutant.
    const auto key = static_cast<std::uint16_t>(in.integer<std::uint16_t>() % 96u);
    switch (in.byte() % 8u) {
      case 0: {  // try_emplace -- false on duplicate or full, never asserts
        const bool present_before = set.contains(key);
        const std::size_t size_before = set.size();
        const bool inserted = set.try_emplace(std::uint16_t{key});
        if (inserted) {
          if (present_before) {
            __builtin_trap();  // inserted a key that was already there
          }
          if (set.size() != size_before + 1 || !set.contains(key)) {
            __builtin_trap();
          }
        } else if (set.size() != size_before) {
          __builtin_trap();  // a refused insert must leave the set unchanged
        }
        break;
      }
      case 1: {  // erase -- answers "was it there", not "did it fail"
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
      case 3: {  // lower_bound is where the key is, or where it would go
        const auto* lb = set.lower_bound(key);
        if (lb < set.begin() || lb > set.end()) {
          __builtin_trap();
        }
        if (lb != set.end() && *lb < key) {
          __builtin_trap();  // lower_bound must not point before the key
        }
        if (lb != set.begin() && !(*(lb - 1) < key)) {
          __builtin_trap();  // ...and everything before it must be strictly less
        }
        break;
      }
      case 4: {  // iteration order must match positional order
        std::size_t index = 0;
        for (const auto& value : set) {
          if (value != set.nth(index)) {
            __builtin_trap();
          }
          ++index;
        }
        if (index != set.size()) {
          __builtin_trap();
        }
        break;
      }
      case 5:
      case 6: {  // extra weight on erase: it is where the shifting happens
        (void)set.erase(key);
        break;
      }
      default: {  // clear is 1-in-8, not 1-in-6 -- a set that is emptied too
                  // often never grows big enough for a shift to have far to go
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
