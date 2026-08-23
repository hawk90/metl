// What a METL container costs in RAM, asserted rather than assumed.
//
// WHY THIS EXISTS. METL's flash cost is gated: tools/check_size.py ratchets
// `.text` on the linked cross probe. Its RAM cost was gated by nothing at all.
// `.bss` and `.data` are parsed out of that same ELF and discarded, and stack
// depth is not measured anywhere in the repository.
//
// That gap is not incidental to the design, it is created by it. Invariant I1
// ("no heap") is enforced by proving `malloc` is absent from the image, which
// says nothing about what replaced it. What replaced it is inline storage sized
// by the caller's template argument -- so METL did not remove the memory, it
// moved it somewhere nothing was watching. And the place it moved it to is
// worse in one specific way: the heap answers "did it fit" (`malloc` returns
// null, and METL's whole recoverable-API contract is built on that question),
// while a `fixed_vector` that does not fit in the frame is a stack overflow,
// which on an MCU without an MMU quietly rewrites `.bss`.
//
// WHAT IS ASSERTED, and why in this form. Not raw byte counts: `sizeof` depends
// on the ABI (`size_type` is 8 bytes on the host and 4 on ARM32), so a table of
// literals would either be wrong on half the matrix or padded until it stopped
// meaning anything. What is portable is the *shape* of the cost:
//
//     sizeof(container) <= payload + bookkeeping + alignment slack
//
// with the bookkeeping expressed in `size_type` units. That is the property the
// library actually controls and the one a refactor can silently break -- adding
// one member to `fixed_vector` costs every user of it, on every instantiation,
// forever.
//
// Everything here is a `static_assert`, so it is checked by every compiler in
// the matrix including the cross-syntax jobs, and `main` has nothing to do.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/fixed_deque.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_string.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/function_ref.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr std::size_t kWord = sizeof(std::size_t);

/// Padding a compiler may insert to align the bookkeeping members after the
/// storage array. Bounded by the container's own alignment, never more.
template <typename C>
constexpr std::size_t align_slack() {
  return alignof(C) - 1;
}

/// C++17 has no `[[no_unique_address]]`, so an EMPTY comparator or hasher held
/// as a member still needs a distinct address -- and next to an eight-byte
/// aligned storage array that costs a whole alignment unit, not a byte.
/// `flat_map<u32, u64, 256>` pays 8 bytes for a `std::less` with no members.
///
/// Charged as one unit however many functors there are, because they pack: two
/// empty types are two bytes, and two bytes still round to the same unit.
///
/// This term is not decoration. Leaving it out is what made the first version
/// of this file pass on the host and fail on all four Cortex-M targets: with
/// `size_type` at 4 bytes instead of 8 the slack that had been absorbing the
/// comparator disappeared, and `flat_map` missed its budget by ONE byte.
template <typename C>
constexpr std::size_t functor_slot(std::size_t functors) {
  return functors == 0 ? std::size_t{0} : alignof(C);
}

/// `sizeof(C)` is at most the payload, plus a slot for any empty functor
/// members, plus `words` bookkeeping words, plus what alignment can add.
///
/// Deliberately `<=`: a container that gets SMALLER is not a regression, and
/// this must not be the reason somebody cannot shrink one. The cost of `<=` is
/// a few bytes of slack -- three on ARM32, seven on the host -- which is still
/// tight enough that adding one `size_type` member to any container below
/// breaks the build on both. That is the regression this is here to catch, and
/// it is verified by mutation rather than assumed: see the commit message.
template <typename C>
constexpr bool fits(std::size_t payload, std::size_t words, std::size_t functors = 0) {
  return sizeof(C) <= payload + functor_slot<C>(functors) + (words * kWord) + align_slack<C>();
}

// ---------------------------------------------------------------------------
// Sequence containers: payload plus one or two words. This is the good case,
// and it is worth pinning precisely because it is easy to lose.
//
// Measured on the host (size_type 8): fixed_vector 1032 for 1024 of payload,
// fixed_deque/ring_buffer/fixed_queue 1040, fixed_string<256> 272 for 257.
// ---------------------------------------------------------------------------
static_assert(fits<metl::fixed_vector<u32, 256>>(256 * sizeof(u32), 1),
              "fixed_vector should cost its elements plus a size");
static_assert(fits<metl::fixed_stack<u32, 256>>(256 * sizeof(u32), 1),
              "fixed_stack should cost its elements plus a depth");
static_assert(fits<metl::fixed_string<256>>(256 + 1, 1),
              "fixed_string should cost its characters, the NUL, and a length");

// Two words: these keep a head as well as a size.
static_assert(fits<metl::fixed_deque<u32, 256>>(256 * sizeof(u32), 2),
              "fixed_deque should cost its elements plus head and size");
static_assert(fits<metl::ring_buffer<u32, 256>>(256 * sizeof(u32), 2),
              "ring_buffer should cost its elements plus head and size");
static_assert(fits<metl::fixed_queue<u32, 256>>(256 * sizeof(u32), 2),
              "fixed_queue should cost its elements plus head and size");
// These two carry a `Compare` member, so they pay the empty-functor slot too.
static_assert(fits<metl::fixed_priority_queue<u32, 256>>(256 * sizeof(u32), 2, 1),
              "fixed_priority_queue should cost its elements, a comparator slot "
              "and bookkeeping");
static_assert(fits<metl::flat_set<u32, 256>>(256 * sizeof(u32), 1, 1),
              "flat_set should cost its elements, a comparator slot and a size");

// ---------------------------------------------------------------------------
// flat_map: the payload is NOT key bytes plus value bytes.
//
// It stores `value_type` -- a pair -- so `pair<u32, u64>` is 16 bytes, not 12:
// four bytes of padding between the key and the eight-byte-aligned value. A
// caller who budgets 256 * (4 + 8) = 3072 bytes gets 4112. That is a property
// of the layout, not of METL's bookkeeping, which is why the assertion is
// written against `value_type` and the arithmetic is spelled out.
// ---------------------------------------------------------------------------
// Members, in order: `Compare comp_`, the storage array, `size_type size_`.
// One functor slot, one bookkeeping word -- NOT two, which is the mistake that
// hid the comparator's cost until ARM32 exposed it.
using flat_map_type = metl::flat_map<u32, u64, 256>;
static_assert(fits<flat_map_type>(256 * sizeof(flat_map_type::value_type), 1, 1),
              "flat_map should cost 256 pairs, a comparator slot and a size");
static_assert(sizeof(flat_map_type::value_type) > sizeof(u32) + sizeof(u64) || alignof(u64) <= sizeof(u32),
              "if the pair ever stops being padded, the comment above is stale");

// ---------------------------------------------------------------------------
// The hash containers, where the number is much larger than a caller expects.
//
// `bucket_count` is `bit_ceil(Capacity * 2)`, so the table always holds at
// least twice the requested capacity, and each bucket carries a state byte
// alongside its slot. For `static_unordered_map<u32, u64, 256>` that is
// 512 * 16 bytes of slots + 512 state bytes + bookkeeping = 8728, against the
// 3072 bytes of key-and-value the caller asked to store: **2.84x**.
//
// This is a defensible design -- open addressing with linear probing needs the
// load factor under one half, and a power-of-two count is what lets probing
// mask instead of divide, which matters on a core with no divider. It was just
// never written down anywhere a caller would look. docs/CHOOSING.md now says
// it, and these assertions keep that prose true.
// ---------------------------------------------------------------------------
using map_type = metl::static_unordered_map<u32, u64, 256>;
using set_type = metl::static_unordered_set<u32, 256>;

static_assert(map_type::bucket_count == 512, "the memory documented in docs/CHOOSING.md assumes 2x buckets");
static_assert(set_type::bucket_count == 512, "the memory documented in docs/CHOOSING.md assumes 2x buckets");

// One state byte per bucket on top of the slot itself, then `size_`, then
// `tombstones_`, then the two empty functors (`Hash` and `KeyEqual`) that share
// a single slot at the tail.
static_assert(fits<map_type>(map_type::bucket_count * (sizeof(map_type::value_type) + 1), 2, 2),
              "static_unordered_map should cost bucket_count slots, one state "
              "byte each, two words and a functor slot");
static_assert(fits<set_type>(set_type::bucket_count * (sizeof(set_type::value_type) + 1), 2, 2),
              "static_unordered_set should cost bucket_count slots, one state "
              "byte each, two words and a functor slot");

// ---------------------------------------------------------------------------
// THE CLIFF. `bit_ceil(Capacity * 2)` means asking for one more element can
// double the table. Capacity 128 gets 256 buckets; capacity 129 gets 512.
//
// Measured: static_unordered_map<u32, u64, 128> is 4376 bytes and <..., 129> is
// 8728 -- 4352 more bytes for one more element. At 256 -> 257 it is 8728 ->
// 17432, which on a 32 KB part is half the RAM in the gap between two capacities
// a caller would consider interchangeable.
//
// Asserted here so that it is impossible to change this behaviour without also
// being sent to the paragraph in docs/CHOOSING.md that promises it. If somebody
// removes the cliff, that is an improvement and this assertion is how they find
// out the documentation has to change with it.
// ---------------------------------------------------------------------------
using map_128 = metl::static_unordered_map<u32, u64, 128>;
using map_129 = metl::static_unordered_map<u32, u64, 129>;

static_assert(map_128::bucket_count == 256, "128 elements fit in 256 buckets");
static_assert(map_129::bucket_count == 512, "one more element doubles the table");
static_assert(sizeof(map_129) > sizeof(map_128), "the capacity cliff documented in docs/CHOOSING.md is real");

// And the shape of it: the jump is a doubling, not a rounding.
static_assert(map_129::bucket_count == 2 * map_128::bucket_count,
              "the cliff doubles; docs/CHOOSING.md says so");

// ---------------------------------------------------------------------------
// The instrument must be able to fail. `fits` is only worth having if a
// container one word fatter would be rejected -- otherwise every assertion
// above passes for the wrong reason. This is the same canary discipline the
// CI gates use, applied at compile time.
// ---------------------------------------------------------------------------
struct too_fat {
  std::uint32_t storage[256];
  std::size_t a;
  std::size_t b;
  std::size_t c;
};
static_assert(!fits<too_fat>(256 * sizeof(u32), 1),
              "fits<> accepted three bookkeeping words where one was budgeted -- "
              "it cannot fail, so none of the assertions above mean anything");
static_assert(fits<too_fat>(256 * sizeof(u32), 3),
              "fits<> rejected a container that is exactly within its budget");

// #17 shrank function_ref to a two-pointer union layout ("perf(function_ref):
// shrink to 2-pointer union layout (P0792)"). That is a claim about sizeof, and
// it is the only claim of its kind in the log that nothing here was holding --
// this file pins eleven containers and did not pin the type whose whole commit
// was about its size.
//
// Stated as an exact equality rather than a bound: two pointers is the design,
// and one more word would be a regression worth a conversation even though it
// would still "fit".
static_assert(sizeof(metl::function_ref<void()>) == 2 * sizeof(void*),
              "function_ref is a two-pointer union (P0792); #17 shrank it to this");
static_assert(sizeof(metl::function_ref<int(int, int)>) == 2 * sizeof(void*),
              "the signature must not change the footprint -- it is erased");
static_assert(alignof(metl::function_ref<void()>) == alignof(void*),
              "a two-pointer union aligns like a pointer");

}  // namespace

int main() {
  // Everything above is compile-time. This runs so the footprint check appears
  // in the ctest list rather than being invisible inside somebody else's TU.
  CHECK(sizeof(metl::fixed_vector<u32, 256>) >= 256 * sizeof(u32));
  return metl_test::exit_code();
}
