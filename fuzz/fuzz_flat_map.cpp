// libFuzzer harness for metl::flat_map, checked against a reference model.
//
// Drives a fixed-capacity sorted flat map with an opcode stream of
// CONTRACT-VALID operations only: try_emplace / try_insert_or_assign / erase are
// bool/return-based (never assert), find/contains are total, and positional
// operator[]/nth are always bounded by `% size()`. emplace (which asserts on a
// full map or duplicate key) is deliberately NOT called.
//
// WHAT CHANGED, and why the old version was not enough. This harness used to
// check sorted order, `size()`, and that `find` and `contains` agreed with each
// other. All three hold for a map that returns the WRONG VALUE: shift every
// stored value by one slot and the keys stay sorted, the size stays right, and
// find and contains still agree. It was a memory-safety oracle, not a
// correctness one, and nothing in the repository could tell a correct answer
// from a plausible one.
//
// Every operation now goes through metl_fuzz::map_oracle, which performs it on
// a std::map as well and compares the two afterwards in both directions.
//
// The encoding does move: the opcode is now `% 8` rather than `% 7` and the key
// is folded into a small space (see below). Existing corpus inputs therefore
// mean something slightly different than they did -- they consume the same
// bytes at the same offsets, so they stay well-formed and keep their length and
// structure, but an input that used to drive `clear` may now drive `erase_run`.
// That is a fair trade for reaching paths the old key space made unreachable,
// and libFuzzer re-derives coverage for a corpus on load either way.

#include "fuzz_helpers.hpp"
#include "fuzz_model.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>

#include <metl/flat_map.hpp>

namespace {

constexpr std::size_t kCapacity = 32;
using key_type = std::uint16_t;
using mapped_type = std::uint32_t;
using map_type = metl::flat_map<key_type, mapped_type, kCapacity>;

// flat_map's sorted order is part of its contract, not an implementation
// detail, so the model comparison checks position by position.
using oracle_type = metl_fuzz::map_oracle<map_type, key_type, mapped_type, true>;

// KEY SPACE, and why it is narrowed. The paths worth reaching in a sorted flat
// container -- erasing a key that is present, inserting into the middle, and
// the element shift each of those forces -- all require the drawn key to
// already be in the map. Across a 16-bit key space and 32 slots that is about
// 0.05% per operation, so erase almost always named an absent key and returned
// false without touching anything.
//
// Folding the key into a small space makes those paths ordinary. The full width
// is still read, so bytes line up with the corpus that already exists; only the
// interpretation of them changes.
constexpr key_type kKeySpace = 64;  // 2x capacity: dense enough that erases hit

// Consecutive erases, in one opcode. Draining is a real usage pattern and it
// walks the shift path repeatedly, which a stream of independent single erases
// separated by inserts does not.
constexpr std::size_t kMaxEraseRun = 32;

// Kept from the original harness. The oracle proves the container equals the
// model; this proves the model is not being compared against something that
// merely looks sorted -- a check on the container in its own terms.
void check_sorted(const map_type& map) {
  metl_fuzz::require(map.size() <= map.capacity());
  for (std::size_t i = 1; i < map.size(); ++i) {
    metl_fuzz::require(map[i - 1].key < map[i].key);
  }
}

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
      case 1: {  // try_insert_or_assign — false only if a NEW key cannot fit
        oracle.try_insert_or_assign(key, in.integer<mapped_type>());
        break;
      }
      case 2: {  // erase — false if absent
        oracle.erase(key);
        break;
      }
      case 3: {  // find / contains, and the VALUE behind them
        oracle.lookup(key);
        break;
      }
      case 4: {  // positional access, always bounded
        if (!map.empty()) {
          const std::size_t idx = in.byte() % map.size();
          // nth(i) must name the same element the model has at position i.
          // Sorted order is the whole reason a caller would index into this
          // container rather than look a key up.
          auto expected = oracle.model().begin();
          std::advance(expected, static_cast<std::ptrdiff_t>(idx));
          metl_fuzz::require(map.nth(idx).key == expected->first);
          metl_fuzz::require(map.nth(idx).value == expected->second);
        }
        break;
      }
      case 5: {  // full iteration
        std::uint64_t acc = 0;
        for (const auto& item : map) {
          acc += item.value;
        }
        std::uint64_t expected = 0;
        for (const auto& entry : oracle.model()) {
          expected += entry.second;
        }
        metl_fuzz::require(acc == expected);
        break;
      }
      case 6: {  // erase_run — consecutive erases, walking the shift path
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
    check_sorted(map);
  }

  oracle.verify();
  return 0;
}
