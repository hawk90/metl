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

/// `sizeof(C)` is at most the payload plus `words` bookkeeping words plus what
/// alignment can add. Deliberately `<=`: a container that gets SMALLER is not a
/// regression, and this must not be the reason somebody cannot shrink one.
template <typename C>
constexpr bool fits(std::size_t payload, std::size_t words) {
  return sizeof(C) <= payload + (words * kWord) + align_slack<C>();
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
static_assert(fits<metl::fixed_priority_queue<u32, 256>>(256 * sizeof(u32), 2),
              "fixed_priority_queue should cost its elements plus bookkeeping");
static_assert(fits<metl::flat_set<u32, 256>>(256 * sizeof(u32), 2),
              "flat_set should cost its elements plus bookkeeping");

// ---------------------------------------------------------------------------
// flat_map: the payload is NOT key bytes plus value bytes.
//
// It stores `value_type` -- a pair -- so `pair<u32, u64>` is 16 bytes, not 12:
// four bytes of padding between the key and the eight-byte-aligned value. A
// caller who budgets 256 * (4 + 8) = 3072 bytes gets 4112. That is a property
// of the layout, not of METL's bookkeeping, which is why the assertion is
// written against `value_type` and the arithmetic is spelled out.
// ---------------------------------------------------------------------------
using flat_map_type = metl::flat_map<u32, u64, 256>;
static_assert(fits<flat_map_type>(256 * sizeof(flat_map_type::value_type), 2),
              "flat_map should cost 256 pairs plus bookkeeping");
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

// One state byte per bucket, on top of the slot itself.
static_assert(fits<map_type>(map_type::bucket_count * (sizeof(map_type::value_type) + 1), 3),
              "static_unordered_map should cost bucket_count slots, one state "
              "byte each, plus bookkeeping");
static_assert(fits<set_type>(set_type::bucket_count * (sizeof(set_type::value_type) + 1), 3),
              "static_unordered_set should cost bucket_count slots, one state "
              "byte each, plus bookkeeping");

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

}  // namespace

int main() {
  // Everything above is compile-time. This runs so the footprint check appears
  // in the ctest list rather than being invisible inside somebody else's TU.
  CHECK(sizeof(metl::fixed_vector<u32, 256>) >= 256 * sizeof(u32));
  return metl_test::exit_code();
}
