// Instructions the Cortex-M build executes per benchmark region, counted under
// QEMU (#338).
//
// tools/check_instructions.py counts the HOST build's instructions with
// cachegrind, which gates the algorithm but not the ARM code METL ships. This
// probe is the target half: the same kind of fixed-work region, cross-compiled,
// run under qemu-system-arm, with every guest instruction between two markers
// counted by tools/check_qemu_probes.py.
//
// HOW A REGION IS DELIMITED. `metl_region_begin` and `metl_region_end` are
// noinline functions with nothing in them. The tool runs QEMU with one
// instruction per translation block and `-d exec,nochain`, so every executed
// guest instruction is one log line carrying its PC, and counts the lines from
// the entry of `metl_region_begin` to the entry of `metl_region_end`. The
// markers' own call and return are a constant few instructions in every region;
// they do not move with METL and are left in rather than estimated away.
//
// THE WORK MUST SURVIVE -Os. Containers live in static storage and every result
// goes to a volatile sink, so the compiler cannot prove a push and its pop
// cancel and delete both -- the failure tools/check_instructions.py's own
// ring_buffer benchmark records twice.
//
// What this does NOT measure: time. QEMU executes instructions; it does not
// model wait states, caches, pipelines or interrupt latency. These are
// instruction counts and are reported as nothing else.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <metl/fixed_queue.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/span.hpp>
#include <metl/spsc_byte_ring.hpp>
#include <metl/spsc_queue.hpp>

// mpmc_queue refuses, at compile time, a core without a lock-free CAS
// (ARMv6-M). Its region is absent there, and the tool expects exactly that.
#if !defined(__ARM_ARCH_6M__)
#define METL_BENCH_HAS_MPMC 1
#include <metl/mpmc_queue.hpp>
#endif

namespace {

using u32 = std::uint32_t;

/// Iterations per region. Fixed, so the count is a function of the source and
/// the toolchain only.
constexpr u32 kIterations = 64;

volatile u32 g_sink = 0;

void sink(u32 value) noexcept {
  g_sink = g_sink + value;
}

metl::fixed_vector<u32, 64> g_vector;
metl::fixed_queue<u32, 64> g_queue;
metl::ring_buffer<u32, 64> g_ring;
metl::flat_map<u32, u32, 64> g_flat_map;
metl::spsc_queue<u32, 64> g_spsc;
metl::spsc_byte_ring<256> g_bytes;
#if defined(METL_BENCH_HAS_MPMC)
metl::mpmc_queue<u32, 64> g_mpmc;
#endif

__attribute__((noinline)) void fixed_vector_fill_clear() noexcept {
  for (u32 i = 0; i < kIterations; ++i) {
    sink(g_vector.try_push_back(i) ? 1u : 0u);
  }
  sink(static_cast<u32>(g_vector.size()));
  g_vector.clear();
}

__attribute__((noinline)) void fixed_queue_push_pop() noexcept {
  for (u32 i = 0; i < kIterations; ++i) {
    sink(g_queue.try_push(i) ? 1u : 0u);
    sink(g_queue.front());
    g_queue.pop();
  }
}

__attribute__((noinline)) void ring_buffer_push_overwrite() noexcept {
  for (u32 i = 0; i < kIterations; ++i) {
    sink(g_ring.push_overwrite(i));
  }
}

__attribute__((noinline)) void flat_map_insert_find() noexcept {
  for (u32 i = 0; i < kIterations; ++i) {
    // Descending keys: every insert lands at the front and shifts the rest.
    sink(g_flat_map.try_emplace(kIterations - i, i) ? 1u : 0u);
  }
  for (u32 i = 0; i < kIterations; ++i) {
    const u32* found = g_flat_map.find(i + 1u);
    sink(found != nullptr ? *found : 0u);
  }
  g_flat_map.clear();
}

__attribute__((noinline)) void spsc_queue_push_pop() noexcept {
  for (u32 i = 0; i < kIterations; ++i) {
    sink(g_spsc.try_push(i) ? 1u : 0u);
    u32 out = 0;
    sink(g_spsc.try_pop(out) ? out : 0u);
  }
}

__attribute__((noinline)) void spsc_byte_ring_write_read() noexcept {
  const std::byte chunk[8] = {};
  for (u32 i = 0; i < kIterations; ++i) {
    sink(g_bytes.try_write(metl::span<const std::byte>(chunk, sizeof(chunk))) ? 1u : 0u);
    const auto readable = g_bytes.readable_span();
    sink(static_cast<u32>(readable.size()));
    g_bytes.consume(readable.size());
  }
}

#if defined(METL_BENCH_HAS_MPMC)
__attribute__((noinline)) void mpmc_queue_push_pop() noexcept {
  for (u32 i = 0; i < kIterations; ++i) {
    sink(g_mpmc.try_push(i) ? 1u : 0u);
    u32 out = 0;
    sink(g_mpmc.try_pop(out) ? out : 0u);
  }
}
#endif

struct region {
  const char* name;
  void (*body)() noexcept;
};

// The names are what the budgets are keyed by. The tool reads them from the
// METL_BENCH_REGIONS line below, never from a copy of its own.
const region kRegions[] = {
    {"fixed_vector_fill_clear", fixed_vector_fill_clear},
    {"fixed_queue_push_pop", fixed_queue_push_pop},
    {"ring_buffer_push_overwrite", ring_buffer_push_overwrite},
    {"flat_map_insert_find", flat_map_insert_find},
    {"spsc_queue_push_pop", spsc_queue_push_pop},
    {"spsc_byte_ring_write_read", spsc_byte_ring_write_read},
#if defined(METL_BENCH_HAS_MPMC)
    {"mpmc_queue_push_pop", mpmc_queue_push_pop},
#endif
};

}  // namespace

// The markers. extern "C" so the tool finds them by an unmangled name; noinline
// and non-empty to the compiler (the asm), so a call to each really happens and
// its entry is an instruction the trace can see.
extern "C" __attribute__((noinline)) void metl_region_begin(void) {
  __asm__ __volatile__("" ::: "memory");
}

extern "C" __attribute__((noinline)) void metl_region_end(void) {
  __asm__ __volatile__("" ::: "memory");
}

int main() {
  std::printf("METL_BENCH_REGIONS");
  for (const region& r : kRegions) {
    std::printf(" %s", r.name);
  }
  std::printf("\n");
  for (const region& r : kRegions) {
    metl_region_begin();
    r.body();
    metl_region_end();
  }
  return 0;
}
