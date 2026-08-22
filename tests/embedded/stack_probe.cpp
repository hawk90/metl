// What METL puts on the stack while it works.
//
// The other half of the RAM question. `tests/core/ram_footprint_test.cpp` pins
// what a container COSTS -- the storage the caller declares, which is a
// property of `sizeof` and is asserted at compile time. This file is about
// something `sizeof` cannot see: the memory METL uses TRANSIENTLY inside an
// operation, in the frame of whatever function is running.
//
// The hazard is concrete and has a name. `static_unordered_map::erase` reclaims
// tombstones by calling `rehash_in_place`, and the whole point of "in place" is
// that it rebuilds the table without a second copy. The obvious, simpler
// implementation -- build the new table in a local array and copy it back -- is
// correct, passes every test in this repository, passes every fuzz harness, and
// would put `bucket_count * sizeof(value_type)` on the stack. For
// `static_unordered_map<uint32_t, uint64_t, 256>` that is EIGHT KILOBYTES, in a
// function a caller reaches through `erase`. On a part with 8 KB of SRAM, that
// is the whole of it.
//
// Nothing in this repository could see that happen. Which is the same situation
// #74 found for the reclaim itself: correct, slower, invisible.
//
// THREE THINGS THIS FILE DOES ON PURPOSE, each of which the obvious version
// gets wrong:
//
//   1. Every container is in STATIC storage, never a local. A container
//      declared as a local IS a stack frame -- an 8728-byte one -- and it would
//      swamp the measurement with the caller's own choice. What is left after
//      that is METL's own use, which is what this measures.
//
//   2. The capacities are REALISTIC, not the capacity 4-8 the invariant probe
//      uses. This matters more than it looks: at capacity 8 a rehash that built
//      into a local array would need about 128 bytes, which is indistinguishable
//      from ordinary register spilling. The defect only becomes visible at a
//      capacity somebody would really use. A gate that cannot tell the bug from
//      the noise is not a gate.
//
//   3. The check takes the maximum frame over the WHOLE translation unit, not
//      just frames attributed to a metl/ header. Under `-Os` most of METL is
//      inlined into its caller, so a stack temporary inside `rehash_in_place`
//      is charged to whichever function it landed in. Filtering by filename
//      would quietly stop measuring the thing this exists to measure.
//
// There is no `main`. This TU is compiled, not linked: `-fstack-usage` is a
// compile-stage flag, and every frame it reports is emitted before any linker
// runs.

#include <cstddef>
#include <cstdint>

#include <metl/fixed_deque.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_string.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/format.hpp>
#include <metl/handle_pool.hpp>
#include <metl/object_pool.hpp>
#include <metl/parse.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/span.hpp>
#include <metl/spsc_byte_ring.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

/// Capacities a real caller would pick. See note 2 above: too small and the
/// defect this gate exists to catch hides inside register spilling.
constexpr std::size_t kTable = 256;
constexpr std::size_t kSeq = 256;

// Static storage, deliberately. See note 1: a local container is the caller's
// frame, not METL's, and it would drown out everything measured here.
metl::static_unordered_map<u32, u64, kTable> g_map;
metl::static_unordered_set<u32, kTable> g_set;
metl::flat_map<u32, u64, kTable> g_flat_map;
metl::flat_set<u32, kTable> g_flat_set;
metl::fixed_vector<u32, kSeq> g_vector;
metl::fixed_deque<u32, kSeq> g_deque;
metl::ring_buffer<u32, kSeq> g_ring;
metl::fixed_priority_queue<u32, kSeq> g_heap;
metl::fixed_string<kSeq> g_string;
metl::object_pool<u64, 128> g_object_pool;
metl::handle_pool<u64, 128> g_handle_pool;
metl::spsc_byte_ring<512> g_bytes;

/// Consume a value so nothing above is optimised away as dead.
volatile u32 g_sink = 0;

void sink(u32 value) noexcept {
  g_sink = g_sink + value;
}

// `noinline` so each operation gets a frame of its own in the .su file. Without
// it the compiler is free to fold them all into one, and a single fat frame
// would be reported without saying which operation earned it.

/// THE ONE THAT MATTERS. Insert to fill, then erase everything, repeatedly --
/// which is what drives tombstones past `bucket_count / 8` and fires
/// `rehash_in_place`. If that rebuild ever stops being in place, it is this
/// frame that grows.
__attribute__((noinline)) void churn_map() noexcept {
  for (int round = 0; round < 4; ++round) {
    for (u32 i = 0; i < 200; ++i) {
      sink(g_map.try_emplace(i, i) ? 1u : 0u);
    }
    for (u32 i = 0; i < 200; ++i) {
      sink(g_map.erase(i) ? 1u : 0u);
    }
  }
}

__attribute__((noinline)) void churn_set() noexcept {
  for (int round = 0; round < 4; ++round) {
    for (u32 i = 0; i < 200; ++i) {
      sink(g_set.try_emplace(i) ? 1u : 0u);
    }
    for (u32 i = 0; i < 200; ++i) {
      sink(g_set.erase(i) ? 1u : 0u);
    }
  }
}

/// The flat containers shift on insert and erase. A shift implemented through a
/// temporary buffer rather than element-by-element would show up here.
__attribute__((noinline)) void churn_flat() noexcept {
  for (u32 i = 0; i < 200; ++i) {
    sink(g_flat_map.try_emplace(i, i) ? 1u : 0u);
    sink(g_flat_set.try_emplace(i) ? 1u : 0u);
  }
  for (u32 i = 0; i < 200; ++i) {
    sink(g_flat_map.erase(i) ? 1u : 0u);
    sink(g_flat_set.erase(i) ? 1u : 0u);
  }
}

/// Heap sift up and down: recursion here, or a scratch array, would be visible.
__attribute__((noinline)) void churn_heap() noexcept {
  for (u32 i = 0; i < 200; ++i) {
    sink(g_heap.try_push(200u - i) ? 1u : 0u);
  }
  while (!g_heap.empty()) {
    sink(g_heap.top());
    g_heap.pop();
  }
}

__attribute__((noinline)) void churn_sequences() noexcept {
  for (u32 i = 0; i < 200; ++i) {
    sink(g_vector.try_push_back(i) ? 1u : 0u);
    sink(g_deque.try_emplace_front(i) ? 1u : 0u);
    g_ring.push_overwrite(i);
  }
  g_vector.clear();
  g_deque.clear();
  g_ring.clear();
}

__attribute__((noinline)) void churn_pools() noexcept {
  for (u32 i = 0; i < 64; ++i) {
    u64* slot = g_object_pool.try_emplace(i);
    sink(slot != nullptr ? 1u : 0u);
    if (slot != nullptr) {
      sink(g_object_pool.destroy(slot) ? 1u : 0u);
    }
    const auto handle = g_handle_pool.try_emplace(i);
    sink(handle.valid() ? 1u : 0u);
    sink(g_handle_pool.destroy(handle) ? 1u : 0u);
  }
}

/// Text conversion, where a scratch buffer is the natural implementation and
/// would be entirely reasonable -- but should be a documented, bounded one, not
/// a surprise found on a 2 KB stack.
__attribute__((noinline)) void churn_text() noexcept {
  char buffer[64] = {};
  const metl::span<char> out(buffer, sizeof(buffer));

  sink(static_cast<u32>(metl::try_format_uint(out, 4294967295u).size()));
  sink(static_cast<u32>(metl::try_format_int(out, -2147483647).size()));
  sink(static_cast<u32>(metl::try_format_hex(out, 0xDEADBEEFu).size()));

  const char digits[] = "4294967295";
  const auto parsed = metl::try_parse_uint<u32>(metl::span<const char>(digits, sizeof(digits) - 1));
  sink(parsed.has_value() ? parsed->value : 0u);

  sink(g_string.try_append("metl") ? 1u : 0u);
  g_string.clear();
}

__attribute__((noinline)) void churn_bytes() noexcept {
  const metl::span<std::byte> writable = g_bytes.writable_span();
  if (!writable.empty()) {
    g_bytes.commit_write(writable.size());
  }
  sink(static_cast<u32>(g_bytes.readable_span().size()));
}

}  // namespace

/// Named so a reader of the .su file can see every operation is reached. Not
/// `main`: this TU is compiled, never linked.
extern "C" void metl_stack_probe_drive(void) {
  churn_map();
  churn_set();
  churn_flat();
  churn_heap();
  churn_sequences();
  churn_pools();
  churn_text();
  churn_bytes();
}
