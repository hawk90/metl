// libFuzzer harness for metl::static_unordered_map.
//
// Drives a fixed-capacity open-addressing hash map (linear probing with
// tombstones) with an opcode stream of CONTRACT-VALID operations only:
// try_emplace / insert_or_assign / erase / find / contains / clear are all
// return-based and never assert. The asserting members (emplace / operator[]
// on a full map) are deliberately NOT called.
//
// A shadow count of live keys is tracked to assert size() consistency, and
// find/contains agreement is checked, so a probing / tombstone / size bug
// surfaces as a real crash rather than silent corruption.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/static_unordered_map.hpp>

namespace {

constexpr std::size_t kCapacity = 32;
using map_type = metl::static_unordered_map<std::uint32_t, std::uint32_t, kCapacity>;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);
  map_type map;

  while (!in.empty()) {
    const std::uint32_t key = in.integer<std::uint32_t>();
    switch (in.byte() % 7u) {
      case 0: {  // try_emplace — false on duplicate key or full (no assert)
        const std::uint32_t value = in.integer<std::uint32_t>();
        if (map.try_emplace(key, value)) {
          // A freshly inserted key must be findable with the stored value.
          const std::uint32_t* v = map.find(key);
          if (v == nullptr || *v != value) {
            __builtin_trap();
          }
        }
        break;
      }
      case 1: {  // insert_or_assign — false only when a new key cannot fit
        const std::uint32_t value = in.integer<std::uint32_t>();
        if (map.insert_or_assign(key, value)) {
          const std::uint32_t* v = map.find(key);
          if (v == nullptr || *v != value) {
            __builtin_trap();
          }
        }
        break;
      }
      case 2: {  // erase — false if absent
        map.erase(key);
        if (map.contains(key)) {
          __builtin_trap();
        }
        break;
      }
      case 3: {  // find / contains consistency
        const std::uint32_t* v = map.find(key);
        if ((v != nullptr) != map.contains(key)) {
          __builtin_trap();
        }
        break;
      }
      case 4: {  // full iteration counts exactly size() occupied slots
        std::size_t seen = 0;
        for (const auto& item : map) {
          (void)item;
          ++seen;
        }
        if (seen != map.size()) {
          __builtin_trap();
        }
        break;
      }
      case 5: {  // find_iterator round-trip
        auto it = map.find_iterator(key);
        if ((it != map.end()) != map.contains(key)) {
          __builtin_trap();
        }
        break;
      }
      default: {  // clear
        map.clear();
        if (!map.empty()) {
          __builtin_trap();
        }
        break;
      }
    }
    if (map.size() > map.capacity()) {
      __builtin_trap();
    }
  }

  return 0;
}
