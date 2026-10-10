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
// The handler longjmps out before the abort, as tests/core/harden_floor_none_test.cpp
// does, so the negative half runs everywhere. It used to fork, and on a host
// without fork() it checked nothing and still passed.

#undef METL_HARDENING  // this test pins its own level, whatever the build passes
#define METL_HARDENING 0
#include "metl_check.hpp"

#include <csetjmp>
#include <cstddef>

#include <metl/assert.hpp>
#include <metl/spsc_byte_ring.hpp>

namespace {

/// Leave the ring empty with both indices at `offset`, so the free region wraps.
void rotate_to(metl::spsc_byte_ring<8>& ring, std::size_t offset) {
  std::size_t moved = 0;
  while (moved < offset) {
    const metl::span<std::byte> out = ring.writable_span();
    CHECK(!out.empty());
    if (out.empty()) {
      return;  // a zero-byte chunk would spin forever instead of failing
    }
    const std::size_t chunk = (offset - moved) < out.size() ? (offset - moved) : out.size();
    ring.commit_write(chunk);
    ring.consume(chunk);
    moved += chunk;
  }
}

std::jmp_buf g_jump;
bool g_fired = false;

void capture(const char* /*expr*/, const char* /*file*/, int /*line*/) noexcept {
  g_fired = true;
  std::longjmp(g_jump, 1);
}

metl::spsc_byte_ring<8> g_ring;  // outside main: setjmp leaves a modified local indeterminate

}  // namespace

int main() {
  // First, establish that the seam state this test depends on is real: with both
  // indices at 6 the ring is empty (8 free) but only 2 of those are contiguous.
  // If this ever stops holding, the guarded call below would test nothing.
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

  rotate_to(g_ring, 6);
  metl::set_assert_handler(&capture);
  g_fired = false;
  if (setjmp(g_jump) == 0) {
    // 8 bytes free, 2 contiguous. Committing 8 is what the old, looser guard
    // allowed. Only METL_HARDEN stands between this and six bytes of stale
    // storage being published as received data.
    g_ring.commit_write(8);
  }
  CHECK(g_fired);

  return metl_test::exit_code();
}
