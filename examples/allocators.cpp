// allocators.cpp
//
// Bounded, heap-free allocation for embedded / real-time code:
//
//   - metl::monotonic_buffer — bump-pointer allocator. Hand out memory in one
//     direction, then reset() the whole buffer at once. Ideal for a
//     "per-frame" / "per-tick" scratch arena that is wiped each loop.
//   - metl::arena_allocator  — bump allocator that also records destructors
//     and supports LIFO mark()/rewind(), so you can unwind to a checkpoint and
//     have non-trivial objects destroyed correctly.
//
// Both live entirely in inline storage (template capacity in bytes): no malloc,
// no global new, deterministic timing. Allocation failure is reported by a
// null return / assert, never an exception.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <metl/arena_allocator.hpp>
#include <metl/monotonic_buffer.hpp>

namespace {

// A non-trivial type so we can observe destructor bookkeeping in the arena.
struct connection {
  static int live;
  int id;
  explicit connection(int i) noexcept : id(i) { ++live; }
  ~connection() { --live; }
};
int connection::live = 0;

// A small fixed block of scratch samples.
struct sample_block {
  std::int32_t v[4];
};

// ---- monotonic_buffer: per-tick scratch memory -----------------------------
int demo_monotonic() {
  // 256 bytes of scratch space reused every control-loop tick.
  metl::monotonic_buffer<256> scratch;

  for (int tick = 0; tick < 3; ++tick) {
    // try_emplace<T> constructs in place and returns nullptr if it would not
    // fit — the caller decides how to degrade (drop work, log, etc.).
    auto* samples = scratch.try_emplace<sample_block>();
    if (samples == nullptr) {
      return 1;
    }
    for (int i = 0; i < 4; ++i) {
      samples->v[i] = tick * 10 + i;
    }
    if (samples->v[3] != tick * 10 + 3) {
      return 2;
    }

    // Raw byte allocation is also available (returns void*, respects alignment).
    void* raw = scratch.allocate(16, alignof(std::max_align_t));
    if (raw == nullptr) {
      return 3;
    }

    // The buffer only grows until we reset it; used() reflects that.
    if (scratch.used() == 0) {
      return 4;
    }

    // Wipe the whole tick's scratch in O(1). monotonic_buffer does NOT run
    // destructors, so only use it for trivially destructible scratch data.
    scratch.reset();
    if (scratch.used() != 0) {
      return 5;
    }
  }
  return 0;
}

// ---- arena_allocator: scoped allocation with destructor unwind -------------
int demo_arena() {
  metl::arena_allocator<512> arena;

  // Long-lived object allocated at the base of the arena.
  auto* base = arena.try_emplace<connection>(1);
  if (base == nullptr || connection::live != 1) {
    return 10;
  }

  // Take a checkpoint, then allocate a batch of temporaries above it.
  const auto checkpoint = arena.mark();
  for (int i = 0; i < 5; ++i) {
    if (arena.try_emplace<connection>(100 + i) == nullptr) {
      return 11;
    }
  }
  if (connection::live != 6) {
    return 12;  // base + 5 temporaries
  }

  // rewind() runs the destructors of everything allocated since the mark, in
  // LIFO order, and reclaims their memory. The base object is untouched.
  arena.rewind(checkpoint);
  if (connection::live != 1 || base->id != 1) {
    return 13;
  }

  // reset() unwinds everything back to empty.
  arena.reset();
  if (connection::live != 0 || !arena.empty()) {
    return 14;
  }
  return 0;
}

}  // namespace

int main() {
  if (int rc = demo_monotonic()) {
    std::fprintf(stderr, "monotonic_buffer demo failed: %d\n", rc);
    return rc;
  }
  if (int rc = demo_arena()) {
    std::fprintf(stderr, "arena_allocator demo failed: %d\n", rc);
    return rc;
  }

  std::printf("allocators: monotonic_buffer + arena_allocator OK\n");
  return 0;
}
