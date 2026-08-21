// Negative control for spsc_byte_ring::commit_write's bound.
//
// This exists because the bound was wrong once, in a way no positive test could
// see: the doc comment said "at most the span you were given", and the guard
// checked "at most the total free space". Those differ exactly at the seam --
// eight bytes free of which only two are contiguous -- so `commit_write(8)` after
// a two-byte span was accepted, publishing six bytes that were never written and
// could not have been. The fix bounds by the contiguous run; this file is what
// keeps it bounded.
//
// `METL_HARDENING 0` is set before the include on purpose: at that level
// METL_ASSERT is compiled out, so if the guard were a METL_ASSERT rather than a
// METL_HARDEN this test would fail. The security floor is part of the claim.
//
// The abort cannot be observed in-process (a failed guard is provably
// [[noreturn]]), so the overflow runs in a forked child that must be killed by
// the signal -- the same technique as tests/core/harden_floor_none_test.cpp.

#define METL_HARDENING 0
#include "metl_check.hpp"

#include <cstddef>

#include <metl/spsc_byte_ring.hpp>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>

#include <sys/wait.h>
#define METL_BYTE_RING_HAVE_FORK 1
#else
#define METL_BYTE_RING_HAVE_FORK 0
#endif

namespace {

/// Leave the ring empty with both indices at `offset`, so the free region wraps.
void rotate_to(metl::spsc_byte_ring<8>& ring, std::size_t offset) {
  std::size_t moved = 0;
  while (moved < offset) {
    const metl::span<std::byte> out = ring.writable_span();
    const std::size_t chunk = (offset - moved) < out.size() ? (offset - moved) : out.size();
    ring.commit_write(chunk);
    ring.consume(chunk);
    moved += chunk;
  }
}

}  // namespace

int main() {
  // First, establish that the seam state this test depends on is real: with both
  // indices at 6 the ring is empty (8 free) but only 2 of those are contiguous.
  // If this ever stops holding, the child below would be testing nothing.
  {
    metl::spsc_byte_ring<8> ring;
    rotate_to(ring, 6);
    CHECK_EQ(ring.writable_size(), 8u);
    CHECK_EQ(ring.writable_span().size(), 2u);
    // Committing exactly the run is legal and must NOT abort.
    ring.commit_write(2);
    CHECK_EQ(ring.readable_size(), 2u);
    ring.consume(2);
  }

#if METL_BYTE_RING_HAVE_FORK
  const pid_t pid = fork();
  if (pid == 0) {
    metl::spsc_byte_ring<8> ring;
    rotate_to(ring, 6);
    // 8 bytes free, 2 contiguous. Committing 8 is what the old, looser guard
    // allowed. Only METL_HARDEN stands between this and six bytes of stale
    // storage being published as received data.
    ring.commit_write(8);
    _exit(0);  // reached only if the guard did NOT fire
  }

  int status = 0;
  (void)waitpid(pid, &status, 0);
  CHECK(WIFSIGNALED(status));
  CHECK(!(WIFEXITED(status) && WEXITSTATUS(status) == 0));
#else
  // No fork here; the positive half above still ran.
#endif

  return metl_test::exit_code();
}
