// libFuzzer harness for metl::fixed_priority_queue.
//
// Drives a fixed-capacity binary heap with an opcode stream of CONTRACT-VALID
// operations only: try_push / try_emplace / erase_if / clear are return-based
// and never assert, and top()/pop() are guarded by empty(). The asserting
// members (push/emplace on a full queue, pop/top on an empty one) are
// deliberately NOT called.
//
// What this harness is actually for: the heap invariant is a *global* property
// of the array that every sift has to restore, and it is exactly the kind of
// property a hand-written test can satisfy by accident. After every mutating
// operation the harness verifies, directly against the storage:
//
//   1. the heap property -- no parent may come after one of its children;
//   2. size bookkeeping -- size() must equal what the operations imply;
//   3. top() -- must be a maximum of the whole array, not merely of slot 0.
//
// A broken sift_up, sift_down or heapify shows up here even when pop order
// happens to look plausible. ASan additionally catches any index that walks off
// the fixed_vector's poisoned tail.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/fixed_priority_queue.hpp>

namespace {

constexpr std::size_t kCapacity = 64;
using queue_type = metl::fixed_priority_queue<std::uint32_t, kCapacity>;

/// No parent may come after one of its children.
void check_heap_property(const queue_type& queue) noexcept {
  const metl::span<const std::uint32_t> slots = queue.as_span();
  for (std::size_t i = 1; i < slots.size(); ++i) {
    if (slots[(i - 1) / 2] < slots[i]) {
      __builtin_trap();
    }
  }
}

/// top() must dominate the entire array, which is stronger than the pairwise
/// heap property and catches a heapify that left a stale root.
void check_top_is_max(const queue_type& queue) noexcept {
  if (queue.empty()) {
    return;
  }
  const metl::span<const std::uint32_t> slots = queue.as_span();
  const std::uint32_t top = queue.top();
  for (std::size_t i = 0; i < slots.size(); ++i) {
    if (slots[i] > top) {
      __builtin_trap();
    }
  }
}

void check(const queue_type& queue, std::size_t expected_size) noexcept {
  if (queue.size() != expected_size || queue.as_span().size() != expected_size) {
    __builtin_trap();
  }
  if (queue.empty() != (expected_size == 0) || queue.full() != (expected_size == kCapacity)) {
    __builtin_trap();
  }
  check_heap_property(queue);
  check_top_is_max(queue);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);
  queue_type queue;
  std::size_t live = 0;

  while (!in.empty()) {
    switch (in.byte() % 5u) {
      case 0: {  // try_push — false when full, never asserts
        if (queue.try_push(in.integer<std::uint32_t>())) {
          ++live;
        }
        break;
      }
      case 1: {  // try_emplace — same contract, in-place construction
        if (queue.try_emplace(in.integer<std::uint32_t>())) {
          ++live;
        }
        break;
      }
      case 2: {  // pop — guarded by empty(), so never an underflow
        if (!queue.empty()) {
          queue.pop();
          --live;
        }
        break;
      }
      case 3: {  // erase_if — removes an arbitrary subset, then re-heapifies
        const std::uint32_t threshold = in.integer<std::uint32_t>();
        const std::size_t removed =
            queue.erase_if([threshold](const std::uint32_t& v) noexcept { return v < threshold; });
        if (removed > live) {
          __builtin_trap();  // removed more than were ever there
        }
        live -= removed;
        break;
      }
      default: {  // clear
        queue.clear();
        live = 0;
        break;
      }
    }
    check(queue, live);
  }

  // Draining must yield a non-increasing sequence and end exactly empty.
  std::uint32_t previous = 0xFFFFFFFFu;
  while (!queue.empty()) {
    const std::uint32_t current = queue.top();
    if (current > previous) {
      __builtin_trap();
    }
    previous = current;
    queue.pop();
    --live;
    check(queue, live);
  }
  if (live != 0) {
    __builtin_trap();
  }

  return 0;
}
