// Real two-thread exercise of metl::spsc_byte_ring, meant to be run under
// ThreadSanitizer (the TSAN CI job) as well as normally.
//
// The ring is deliberately small relative to the traffic, so both sides spend
// most of their time at the full/empty boundary and the seam moves constantly --
// which is where a dropped release/acquire actually shows up. Dropping the
// release on `commit_write` would let the consumer observe the index before the
// bytes; TSAN reports that as a race on the storage, and the byte-order check
// below catches it even without TSAN.
//
// Bounded and deterministic: fixed byte counts, exact sequence assertions, no
// sleeps or timeouts.

#include <cstddef>
#include <cstdint>
#include <thread>

#include <metl/spsc_byte_ring.hpp>

namespace {

constexpr std::size_t kTotalBytes = 200000;

std::uint8_t expected_byte(std::size_t index) noexcept {
  // A cheap non-constant pattern, so a mis-ordered or duplicated chunk cannot
  // pass by accident the way a constant fill would.
  return static_cast<std::uint8_t>((index * 31u + (index >> 8)) & 0xFFu);
}

/// Producer fills through the span protocol; consumer drains through it and
/// verifies every byte. This is the zero-copy path, the reason the type exists.
int test_span_protocol() {
  metl::spsc_byte_ring<64> ring;
  bool order_ok = true;

  // The consumer writes order_ok; main reads it only after join(), which
  // establishes the necessary happens-before edge.
  std::thread consumer([&] {
    std::size_t read = 0;
    while (read < kTotalBytes) {
      const metl::span<const std::byte> in = ring.readable_span();
      if (in.empty()) {
        std::this_thread::yield();
        continue;
      }
      for (std::size_t i = 0; i < in.size(); ++i) {
        if (static_cast<std::uint8_t>(in[i]) != expected_byte(read + i)) {
          order_ok = false;
        }
      }
      ring.consume(in.size());
      read += in.size();
    }
  });

  std::size_t written = 0;
  while (written < kTotalBytes) {
    const metl::span<std::byte> out = ring.writable_span();
    if (out.empty()) {
      std::this_thread::yield();
      continue;
    }
    const std::size_t remaining = kTotalBytes - written;
    const std::size_t chunk = remaining < out.size() ? remaining : out.size();
    for (std::size_t i = 0; i < chunk; ++i) {
      out[i] = static_cast<std::byte>(expected_byte(written + i));
    }
    ring.commit_write(chunk);
    written += chunk;
  }

  consumer.join();
  if (!order_ok) {
    return 1;
  }
  if (!ring.empty() || ring.readable_size() != 0) {
    return 2;
  }
  return 0;
}

/// The copying helpers carry the same ordering, on their own code paths --
/// try_write publishes with its own release store and read() releases with its
/// own, so they need their own exercise rather than inheriting the coverage above.
int test_copy_helpers() {
  metl::spsc_byte_ring<32> ring;
  bool order_ok = true;

  std::thread consumer([&] {
    std::size_t read = 0;
    std::byte buffer[13];  // odd size, so chunk boundaries never align with the ring
    while (read < kTotalBytes) {
      const std::size_t remaining = kTotalBytes - read;
      const std::size_t want = remaining < sizeof buffer ? remaining : sizeof buffer;
      const std::size_t got = ring.read(metl::span<std::byte>(buffer, want));
      if (got == 0) {
        std::this_thread::yield();
        continue;
      }
      for (std::size_t i = 0; i < got; ++i) {
        if (static_cast<std::uint8_t>(buffer[i]) != expected_byte(read + i)) {
          order_ok = false;
        }
      }
      read += got;
    }
  });

  std::size_t written = 0;
  std::byte source[7];  // also odd, and coprime with the consumer's 13
  while (written < kTotalBytes) {
    const std::size_t remaining = kTotalBytes - written;
    const std::size_t chunk = remaining < sizeof source ? remaining : sizeof source;
    for (std::size_t i = 0; i < chunk; ++i) {
      source[i] = static_cast<std::byte>(expected_byte(written + i));
    }
    // All-or-nothing: on a false the ring is unchanged, so simply retry.
    if (!ring.try_write(metl::span<const std::byte>(source, chunk))) {
      std::this_thread::yield();
      continue;
    }
    written += chunk;
  }

  consumer.join();
  if (!order_ok) {
    return 3;
  }
  if (!ring.empty()) {
    return 4;
  }
  return 0;
}

}  // namespace

int main() {
  if (const int rc = test_span_protocol(); rc != 0) {
    return rc;
  }
  if (const int rc = test_copy_helpers(); rc != 0) {
    return rc;
  }
  return 0;
}
