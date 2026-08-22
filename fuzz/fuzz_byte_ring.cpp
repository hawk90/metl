// libFuzzer harness for metl::spsc_byte_ring.
//
// The ring is a driver boundary: bytes arrive from a peripheral, get handed to
// the consumer as a contiguous span, and the accounting between those two facts
// is where it can go wrong. Two things make it worth fuzzing rather than just
// unit-testing.
//
// THE SEAM. `writable_span()` stops at the end of the physical buffer, so the
// span it returns is usually SHORTER than `writable_size()`. Committing more
// than the span held would corrupt the ring, and the header hardens against
// exactly that (`METL_HARDEN(count <= writable_run())`, added after the
// asserting bound and the span bound were found to have drifted apart). A fuzzer
// that drives the ring to arbitrary head/tail offsets crosses that seam
// constantly, which a hand-written test does only where somebody thought to.
//
// THE WRAP. head_ and tail_ are monotonic counters where only the DIFFERENCE is
// meaningful. Sizes are computed as `tail - head` in unsigned arithmetic, so the
// accounting has to stay correct as those counters run far past the capacity.
// This harness pushes tens of thousands of bytes through a 64-byte ring for that
// reason.
//
// Single-threaded on purpose. The ring's contract is one producer and one
// consumer; driving both from one thread is a VALID use of it (the ordering
// guarantees are what let them be different threads, not a requirement that they
// are). libFuzzer is single-threaded anyway, and a harness that spawned threads
// would report nondeterministic crashes. Concurrency is TSan's job in ci.yml.
//
// Every operation here is contract-valid: `commit_write` never exceeds the span
// it was given, and `consume` never exceeds `readable_size()`.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/span.hpp>
#include <metl/spsc_byte_ring.hpp>

namespace {

constexpr std::size_t kCapacity = 64;
using ring_type = metl::spsc_byte_ring<kCapacity>;

/// A byte-for-byte model of what the ring should contain, so the harness checks
/// CONTENT and not just accounting. A ring that loses, duplicates or reorders a
/// byte while keeping its sizes consistent would pass an invariant-only check.
class model {
 public:
  void push(std::uint8_t value) {
    if (size_ < kCapacity) {
      data_[(head_ + size_) % kCapacity] = value;
      ++size_;
    }
  }

  std::uint8_t pop() {
    const std::uint8_t value = data_[head_];
    head_ = (head_ + 1) % kCapacity;
    --size_;
    return value;
  }

  std::uint8_t peek(std::size_t offset) const { return data_[(head_ + offset) % kCapacity]; }

  std::size_t size() const { return size_; }

 private:
  std::uint8_t data_[kCapacity] = {};
  std::size_t head_ = 0;
  std::size_t size_ = 0;
};

void check_invariants(const ring_type& ring, const model& expected) {
  if (ring.readable_size() != expected.size()) {
    __builtin_trap();
  }
  if (ring.readable_size() + ring.writable_size() != kCapacity) {
    __builtin_trap();
  }
  // The readable span is a contiguous run, so it can only be shorter than the
  // readable size -- never longer, which would hand the consumer bytes the
  // producer has not published.
  const metl::span<const std::byte> readable = ring.readable_span();
  if (readable.size() > ring.readable_size()) {
    __builtin_trap();
  }
  // ...and the bytes it exposes must be the ones that went in, in order.
  for (std::size_t i = 0; i < readable.size(); ++i) {
    if (static_cast<std::uint8_t>(readable[i]) != expected.peek(i)) {
      __builtin_trap();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);
  ring_type ring;
  model expected;

  while (!in.empty()) {
    switch (in.byte() % 6u) {
      case 0: {  // zero-copy write across the seam
        const metl::span<std::byte> out = ring.writable_span();
        if (out.size() > ring.writable_size()) {
          __builtin_trap();  // the span may be shorter than the free space, never longer
        }
        // Fill a bounded prefix and commit exactly that much -- committing more
        // than the span held is a contract violation, not a bug to find.
        std::size_t want = out.empty() ? 0 : (in.byte() % (out.size() + 1));
        for (std::size_t i = 0; i < want; ++i) {
          const std::uint8_t value = in.byte();
          out[i] = static_cast<std::byte>(value);
          expected.push(value);
        }
        ring.commit_write(want);
        break;
      }
      case 1: {  // copying write, refused rather than truncated when it will not fit
        std::size_t len = 0;
        const std::uint8_t* bytes = in.slice(&len);
        if (len > kCapacity) {
          len = kCapacity;
        }
        std::byte staging[kCapacity];
        for (std::size_t i = 0; i < len; ++i) {
          staging[i] = static_cast<std::byte>(bytes[i]);
        }
        const std::size_t before = ring.readable_size();
        if (ring.try_write(metl::span<const std::byte>(staging, len))) {
          for (std::size_t i = 0; i < len; ++i) {
            expected.push(bytes[i]);
          }
          if (ring.readable_size() != before + len) {
            __builtin_trap();
          }
        } else if (ring.readable_size() != before) {
          __builtin_trap();  // a refused write must leave the ring untouched
        }
        break;
      }
      case 2: {  // zero-copy read, then release exactly what was inspected
        const metl::span<const std::byte> readable = ring.readable_span();
        const std::size_t take = readable.empty() ? 0 : (in.byte() % (readable.size() + 1));
        for (std::size_t i = 0; i < take; ++i) {
          if (static_cast<std::uint8_t>(readable[i]) != expected.peek(i)) {
            __builtin_trap();
          }
        }
        for (std::size_t i = 0; i < take; ++i) {
          (void)expected.pop();
        }
        ring.consume(take);
        break;
      }
      case 3: {  // copying read
        const std::size_t want = in.byte() % (kCapacity + 1);
        std::byte out[kCapacity];
        const std::size_t got = ring.read(metl::span<std::byte>(out, want));
        if (got > want) {
          __builtin_trap();
        }
        for (std::size_t i = 0; i < got; ++i) {
          if (static_cast<std::uint8_t>(out[i]) != expected.pop()) {
            __builtin_trap();
          }
        }
        break;
      }
      case 4: {  // drain everything -- `consume` is bounded by readable_size, not by the run
        const std::size_t all = ring.readable_size();
        for (std::size_t i = 0; i < all; ++i) {
          (void)expected.pop();
        }
        ring.consume(all);
        if (!ring.empty() || ring.readable_size() != 0) {
          __builtin_trap();
        }
        break;
      }
      default: {  // query-only step, so the state machine can idle at any offset
        if (ring.full() && ring.writable_size() != 0) {
          __builtin_trap();
        }
        if (ring.empty() && ring.readable_size() != 0) {
          __builtin_trap();
        }
        break;
      }
    }
    check_invariants(ring, expected);
  }

  return 0;
}
