// METL invariant probe -- the LINKED image the symbol audit runs against.
//
// tools/check_invariants.py reads this image's symbol table and fails if any
// heap / exception / RTTI symbol is present. See docs/SCOPE.md section 7.
//
// Three constraints shape this file, and each one is load-bearing:
//
//   1. NO stdio. A single printf sinks the gate: newlib's vfprintf uses malloc
//      for internal buffering, so the image would contain heap symbols that
//      have nothing to do with METL. (picolibc's tinystdio does not allocate,
//      but the probe must pass under both libcs.) Results are accumulated into
//      a volatile sink and returned from probe_main() instead.
//
//   2. Its own entry point. Linked with -nostartfiles, so no crt0 runs: newlib's
//      exit path (__call_exitprocs) can reference the heap, and rdimon's crt0
//      sets up the heap outright via SYS_HEAPINFO. The image is never executed,
//      so a _start that just calls probe_main and parks is sufficient -- and it
//      leaves libc itself linkable, so any accidental libc heap use still shows.
//
//   3. NO --gc-sections. Deliberate: the audit is strongest when everything the
//      translation units instantiate stays in the image. Section GC would hide
//      an allocation in a path no caller happens to reach.
//
// Every METL object below is function-local on purpose. A namespace-scope
// object with a non-trivial destructor registers through __cxa_atexit, and
// newlib's __register_exitproc can reach _malloc_r -- which would make the gate
// red for a libc reason rather than a METL one. Whether a global
// metl::fixed_vector drags malloc into a newlib image is a real and separately
// interesting question (see docs/SCOPE.md follow-ups); it is deliberately not
// entangled with this gate. For the same reason embedded_smoke.cpp, which holds
// namespace-scope instantiations of every public type, is NOT linked here.

#include <cstddef>
#include <cstdint>

#include <metl/crc32.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/object_pool.hpp>
#include <metl/span.hpp>
#include <metl/spsc_queue.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_map.hpp>

namespace {

// The sink defeats dead-code elimination without needing stdio: every result
// below is folded in, and the total is what probe_main() returns.
volatile std::uint32_t g_sink = 0;

void sink(std::uint32_t value) noexcept {
  g_sink = g_sink + value;
}

void exercise_fixed_vector() noexcept {
  metl::fixed_vector<std::uint32_t, 8> vec;
  for (std::uint32_t i = 0; i < 8; ++i) {
    sink(vec.try_push_back(i) ? 1u : 0u);
  }
  sink(vec.try_push_back(99u) ? 1u : 0u);  // rejected at capacity, never grows
  sink(static_cast<std::uint32_t>(vec.size()));
}

void exercise_spsc_queue() noexcept {
  metl::spsc_queue<std::uint32_t, 8> queue;
  for (std::uint32_t i = 0; i < 8; ++i) {
    sink(queue.try_push(i) ? 1u : 0u);
  }
  std::uint32_t out = 0;
  while (queue.try_pop(out)) {
    sink(out);
  }
}

void exercise_message_queue() noexcept {
  metl::static_message_queue<std::uint32_t, 4> queue;
  for (std::uint32_t i = 0; i < 4; ++i) {
    sink(queue.try_push(i) ? 1u : 0u);
  }
  std::uint32_t out = 0;
  while (queue.try_pop(out)) {
    sink(out);
  }
}

void exercise_object_pool() noexcept {
  metl::object_pool<std::uint32_t, 4> pool;
  std::uint32_t* slots[4] = {nullptr, nullptr, nullptr, nullptr};
  for (std::size_t i = 0; i < 4; ++i) {
    slots[i] = pool.try_emplace(static_cast<std::uint32_t>(i));
    sink(slots[i] != nullptr ? 1u : 0u);
  }
  sink(pool.try_emplace(4u) != nullptr ? 1u : 0u);  // full: must not allocate
  for (std::size_t i = 0; i < 4; ++i) {
    sink(pool.destroy(slots[i]) ? 1u : 0u);
  }
}

void exercise_unordered_map() noexcept {
  metl::static_unordered_map<std::uint32_t, std::uint32_t, 8> map;
  for (std::uint32_t i = 0; i < 8; ++i) {
    sink(map.try_emplace(i, i * 2u) ? 1u : 0u);
  }
  for (std::uint32_t i = 0; i < 8; ++i) {
    const std::uint32_t* found = map.find(i);
    sink(found != nullptr ? *found : 0u);
  }
  // Overflow must fail cleanly rather than rehash onto the heap.
  sink(map.try_emplace(999u, 0u) ? 1u : 0u);
}

void exercise_crc() noexcept {
  const std::uint8_t bytes[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  sink(metl::crc32(metl::span<const std::uint8_t>{bytes, 16}));
  sink(metl::crc32(static_cast<const void*>(bytes), sizeof bytes));
}

}  // namespace

extern "C" int probe_main(void) {
  exercise_fixed_vector();
  exercise_spsc_queue();
  exercise_message_queue();
  exercise_object_pool();
  exercise_unordered_map();
  exercise_crc();
  return static_cast<int>(g_sink);
}

// Freestanding abort. NOT cosmetic -- without it this gate can never be green,
// and the reason is worth knowing:
//
//   METL_ASSERT / METL_HARDEN bottom out in std::abort() unconditionally (see
//   assert.hpp -- even a custom handler is followed by abort() so the path is
//   provably [[noreturn]]). newlib's abort() calls raise(), and newlib's signal
//   machinery allocates its handler table with _malloc_r. So on newlib, ANY
//   image containing a METL assert transitively links malloc and _sbrk.
//
// That is a real property of newlib, not of METL: the probe's own object file
// references nothing but abort(). Supplying abort() is also what a bare-metal
// user does in practice, alongside _exit and the other syscall stubs. Overriding
// it here keeps the gate measuring METL rather than newlib's signal
// implementation -- and the finding itself is recorded in docs/SCOPE.md.
// No [[noreturn]] here on purpose: <cstdlib> (pulled in by assert.hpp) already
// declares abort() noreturn, and repeating the attribute on the definition is
// ill-formed ("attribute does not appear on the first declaration").
extern "C" void abort(void) {
  for (;;) {
  }
}

// Entry point. Never executed -- see note 2 at the top of this file. The park
// loop keeps the linker from needing exit(), whose newlib implementation can
// reach the heap through __call_exitprocs.
extern "C" [[noreturn]] void _start(void) {
  (void)probe_main();
  for (;;) {
  }
}
