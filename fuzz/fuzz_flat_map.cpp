// libFuzzer harness for metl::flat_map.
//
// Drives a fixed-capacity sorted flat map with an opcode stream of
// CONTRACT-VALID operations only: try_emplace / insert_or_assign / erase are
// bool/return-based (never assert), find/contains are total, and positional
// operator[]/nth are always bounded by `% size()`. emplace (which asserts on a
// full map or duplicate key) is deliberately NOT called.
//
// After every op the sorted-order + size invariants are checked, so a shift or
// binary-search bug shows up as a real crash rather than silent corruption.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/flat_map.hpp>

namespace {

constexpr std::size_t kCapacity = 32;
using map_type = metl::flat_map<std::uint16_t, std::uint32_t, kCapacity>;

void check_invariants(const map_type& map) {
  if (map.size() > map.capacity()) {
    __builtin_trap();
  }
  // Elements must be in STRICTLY ascending key order (keys are unique).
  for (std::size_t i = 1; i < map.size(); ++i) {
    if (!(map[i - 1].key < map[i].key)) {
      __builtin_trap();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);
  map_type map;

  while (!in.empty()) {
    const std::uint16_t key = in.integer<std::uint16_t>();
    switch (in.byte() % 7u) {
      case 0: {  // try_emplace — false on duplicate key or full (no assert)
        map.try_emplace(key, in.integer<std::uint32_t>());
        break;
      }
      case 1: {  // insert_or_assign — false only if a new key cannot fit
        map.insert_or_assign(key, in.integer<std::uint32_t>());
        break;
      }
      case 2: {  // erase — false if absent
        map.erase(key);
        break;
      }
      case 3: {  // find / contains consistency
        const std::uint32_t* v = map.find(key);
        if ((v != nullptr) != map.contains(key)) {
          __builtin_trap();
        }
        break;
      }
      case 4: {  // positional access, always bounded
        if (!map.empty()) {
          const std::size_t idx = in.byte() % map.size();
          volatile std::uint32_t v = map.nth(idx).value;
          (void)v;
        }
        break;
      }
      case 5: {  // full iteration
        std::uint64_t acc = 0;
        for (const auto& item : map) {
          acc += item.value;
        }
        (void)acc;
        break;
      }
      default: {  // clear
        map.clear();
        break;
      }
    }
    check_invariants(map);
  }

  return 0;
}
