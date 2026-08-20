// Single-threaded contract tests for metl::spsc_byte_ring.
//
// The concurrency is exercised separately (spsc_byte_ring_threaded_test, run
// under TSAN). What is checked here is the part a threaded test cannot pin down
// deterministically: the WRAP. Every span this type hands out stops at the
// physical end of the buffer, so the interesting states are the ones where the
// free or readable region straddles that seam, and they have to be reached on
// purpose rather than hoped for.

#include "metl/spsc_byte_ring.hpp"

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using ring8 = metl::spsc_byte_ring<8>;

std::byte b(int value) noexcept {
  return static_cast<std::byte>(static_cast<std::uint8_t>(value));
}

int value_of(std::byte byte) noexcept {
  return static_cast<int>(static_cast<std::uint8_t>(byte));
}

/// Fill `count` bytes through the span protocol, numbering them from `first`.
/// Loops because one span stops at the wrap.
void fill(ring8& ring, int first, std::size_t count) {
  std::size_t written = 0;
  while (written < count) {
    const metl::span<std::byte> out = ring.writable_span();
    CHECK(!out.empty());
    const std::size_t chunk = (count - written) < out.size() ? (count - written) : out.size();
    for (std::size_t i = 0; i < chunk; ++i) {
      out[i] = b(first + static_cast<int>(written + i));
    }
    ring.commit_write(chunk);
    written += chunk;
  }
}

/// Drain `count` bytes through the span protocol, checking they are the numbers
/// `first`, `first + 1`, ... Loops for the same reason.
void drain_and_check(ring8& ring, int first, std::size_t count) {
  std::size_t read = 0;
  while (read < count) {
    const metl::span<const std::byte> in = ring.readable_span();
    CHECK(!in.empty());
    const std::size_t chunk = (count - read) < in.size() ? (count - read) : in.size();
    for (std::size_t i = 0; i < chunk; ++i) {
      CHECK_EQ(value_of(in[i]), first + static_cast<int>(read + i));
    }
    ring.consume(chunk);
    read += chunk;
  }
}

}  // namespace

int main() {
  // ---------------------------------------------------------------------
  // Empty ring: the whole buffer is one contiguous writable run.
  // ---------------------------------------------------------------------
  {
    ring8 ring;
    CHECK(ring.empty());
    CHECK(!ring.full());
    CHECK_EQ(ring.capacity(), 8u);
    CHECK_EQ(ring.writable_size(), 8u);
    CHECK_EQ(ring.readable_size(), 0u);
    CHECK_EQ(ring.writable_span().size(), 8u);
    CHECK(ring.readable_span().empty());
  }

  // ---------------------------------------------------------------------
  // THE contract of this type: an empty span means full (or empty) and
  // nothing else. If there is any room at all, the contiguous run is
  // non-empty — otherwise a caller could not tell "wrapped" from "full".
  // ---------------------------------------------------------------------
  {
    ring8 ring;
    for (std::size_t step = 0; step < 8; ++step) {
      // Rotate the indices so every possible seam position is visited.
      fill(ring, 0, 3);
      drain_and_check(ring, 0, 3);

      CHECK_EQ(ring.readable_size(), 0u);
      CHECK(ring.readable_span().empty());  // empty ring -> empty read span
      CHECK_EQ(ring.writable_size(), 8u);
      CHECK(!ring.writable_span().empty());  // room exists -> non-empty write span

      fill(ring, 0, 8);
      CHECK(ring.full());
      CHECK_EQ(ring.writable_size(), 0u);
      CHECK(ring.writable_span().empty());   // full -> empty write span
      CHECK(!ring.readable_span().empty());  // data exists -> non-empty read span
      drain_and_check(ring, 0, 8);
    }
  }

  // ---------------------------------------------------------------------
  // The wrap is visible: span().size() < *_size() exactly at the seam.
  // This is the property the header warns about, pinned to concrete numbers.
  // ---------------------------------------------------------------------
  {
    ring8 ring;
    fill(ring, 0, 6);
    drain_and_check(ring, 0, 6);
    // Both indices are now at 6; 8 bytes are free but only 2 of them are
    // contiguous before the buffer end.
    CHECK_EQ(ring.writable_size(), 8u);
    CHECK_EQ(ring.writable_span().size(), 2u);

    // Fill all 8 across the seam, then the readable side has the mirror shape.
    fill(ring, 100, 8);
    CHECK_EQ(ring.readable_size(), 8u);
    CHECK_EQ(ring.readable_span().size(), 2u);
    // ...and the bytes are still in order across it.
    drain_and_check(ring, 100, 8);
    CHECK(ring.empty());
  }

  // ---------------------------------------------------------------------
  // try_write / write handle the wrap internally, and are all-or-nothing.
  // ---------------------------------------------------------------------
  {
    ring8 ring;
    const std::byte six[6] = {b(1), b(2), b(3), b(4), b(5), b(6)};
    CHECK(ring.try_write(metl::span<const std::byte>(six, 6)));
    CHECK_EQ(ring.readable_size(), 6u);
    drain_and_check(ring, 1, 6);

    // Indices at 6: this 6-byte write must straddle the seam.
    CHECK(ring.try_write(metl::span<const std::byte>(six, 6)));
    CHECK_EQ(ring.readable_size(), 6u);
    CHECK_EQ(ring.readable_span().size(), 2u);  // wrapped, as expected

    // Only 2 bytes free now, so a 6-byte write is refused and changes NOTHING.
    CHECK(!ring.try_write(metl::span<const std::byte>(six, 6)));
    CHECK_EQ(ring.readable_size(), 6u);
    drain_and_check(ring, 1, 6);

    // A zero-length write always succeeds, even on a full ring.
    fill(ring, 0, 8);
    CHECK(ring.full());
    // `0` alone is a null-pointer constant, so the (first, last) overload is
    // viable too; spell the length so the (pointer, size) one is unambiguous.
    CHECK(ring.try_write(metl::span<const std::byte>(six, std::size_t{0})));
    CHECK_EQ(ring.readable_size(), 8u);
    drain_and_check(ring, 0, 8);
  }

  // ---------------------------------------------------------------------
  // read() returns a COUNT: a short read is normal, not a failure.
  // ---------------------------------------------------------------------
  {
    ring8 ring;
    std::byte out[8] = {};

    // Nothing queued: zero, not an error.
    CHECK_EQ(ring.read(metl::span<std::byte>(out, 8)), 0u);

    fill(ring, 10, 3);
    CHECK_EQ(ring.read(metl::span<std::byte>(out, 8)), 3u);  // short: only 3 were there
    CHECK_EQ(value_of(out[0]), 10);
    CHECK_EQ(value_of(out[2]), 12);
    CHECK(ring.empty());

    // Destination smaller than the queued data: read what fits, leave the rest.
    fill(ring, 20, 6);
    CHECK_EQ(ring.read(metl::span<std::byte>(out, 2)), 2u);
    CHECK_EQ(value_of(out[0]), 20);
    CHECK_EQ(value_of(out[1]), 21);
    CHECK_EQ(ring.readable_size(), 4u);
    drain_and_check(ring, 22, 4);

    // Reading across the seam.
    fill(ring, 30, 8);
    CHECK_EQ(ring.read(metl::span<std::byte>(out, 8)), 8u);
    for (int i = 0; i < 8; ++i) {
      CHECK_EQ(value_of(out[static_cast<std::size_t>(i)]), 30 + i);
    }
    CHECK(ring.empty());
  }

  // ---------------------------------------------------------------------
  // Partial commit: a producer that fills less than the span it was given
  // publishes only what it committed.
  // ---------------------------------------------------------------------
  {
    ring8 ring;
    const metl::span<std::byte> out = ring.writable_span();
    CHECK_EQ(out.size(), 8u);
    out[0] = b(7);
    out[1] = b(8);
    // Wrote into 8 bytes' worth of span, but only 2 are real.
    ring.commit_write(2);
    CHECK_EQ(ring.readable_size(), 2u);
    drain_and_check(ring, 7, 2);

    // Committing zero is a no-op, not a special case.
    const metl::span<std::byte> again = ring.writable_span();
    CHECK(!again.empty());
    ring.commit_write(0);
    CHECK(ring.empty());
  }

  // ---------------------------------------------------------------------
  // Byte-exact integrity over many wraps, with an odd chunk size so the seam
  // lands in a different place on every pass.
  // ---------------------------------------------------------------------
  {
    metl::spsc_byte_ring<16> ring;
    int next_written = 0;
    int next_expected = 0;
    for (int round = 0; round < 200; ++round) {
      const std::size_t chunk = static_cast<std::size_t>((round % 7) + 1);  // 1..7
      std::size_t written = 0;
      while (written < chunk) {
        const metl::span<std::byte> out = ring.writable_span();
        if (out.empty()) {
          break;  // full: drain below and continue
        }
        const std::size_t take = (chunk - written) < out.size() ? (chunk - written) : out.size();
        for (std::size_t i = 0; i < take; ++i) {
          out[i] = b(next_written + static_cast<int>(i));
        }
        ring.commit_write(take);
        next_written += static_cast<int>(take);
        written += take;
      }

      // Drain roughly half of what is queued, so the ring never settles into a
      // single alignment.
      std::size_t to_read = ring.readable_size() / 2;
      while (to_read > 0) {
        const metl::span<const std::byte> in = ring.readable_span();
        CHECK(!in.empty());
        const std::size_t take = to_read < in.size() ? to_read : in.size();
        for (std::size_t i = 0; i < take; ++i) {
          // Byte values wrap at 256; compare modulo that.
          CHECK_EQ(value_of(in[i]), (next_expected + static_cast<int>(i)) & 0xFF);
        }
        ring.consume(take);
        next_expected += static_cast<int>(take);
        to_read -= take;
      }

      // The two sides must always agree on how the capacity is divided.
      CHECK_EQ(ring.readable_size() + ring.writable_size(), ring.capacity());
    }
  }

  // ---------------------------------------------------------------------
  // clear() drops everything (single-threaded use only).
  // ---------------------------------------------------------------------
  {
    ring8 ring;
    fill(ring, 0, 5);
    CHECK_EQ(ring.readable_size(), 5u);
    ring.clear();
    CHECK(ring.empty());
    CHECK_EQ(ring.readable_size(), 0u);
    CHECK_EQ(ring.writable_size(), 8u);
    CHECK_EQ(ring.writable_span().size(), 8u);
  }

  return metl_test::exit_code();
}
