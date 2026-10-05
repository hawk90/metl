// The memory-safety floor at METL_HARDENING_NONE (docs/AUDIT.md E.3, G.5).
//
// This TU strips METL_ASSERT. Each case below is a precondition violation that,
// with only METL_ASSERT guarding it, wrote out of bounds:
//
//   - fixed_vector::emplace(pos, ...) on a full vector shifted one element past
//     the storage (and over size_);
//   - pop_back() on an empty vector destroyed data()[-1] and wrapped size_ to
//     SIZE_MAX, so the next push wrote far out of bounds (fixed_stack::pop too);
//   - monotonic_buffer::allocate with a non-power-of-two alignment returned a
//     misaligned pointer.
//
// Each now sits behind METL_HARDEN, which survives NONE. The handler longjmps
// out before the abort -- as tests/core/hardening_common.h does -- so the check
// runs without fork() and therefore on QEMU too.
#define METL_HARDENING 0

#include "metl_check.hpp"

#include <csetjmp>

#include <metl/assert.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/monotonic_buffer.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_message_queue.hpp>

namespace {

std::jmp_buf g_jump;
bool g_fired = false;

void capture(const char* /*expr*/, const char* /*file*/, int /*line*/) noexcept {
  g_fired = true;
  std::longjmp(g_jump, 1);
}

}  // namespace

int main() {
  metl::set_assert_handler(&capture);

  // Positional emplace into a full vector.
  {
    metl::fixed_vector<int, 4> v;
    for (int i = 0; i < 4; ++i) {
      v.push_back(i);
    }
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      v.emplace(v.begin(), 42);
    }
    CHECK(g_fired);
    CHECK_EQ(v.size(), 4u);
    CHECK_EQ(v[0], 0);
  }

  // pop_back on an empty vector, and pop on an empty fixed_stack.
  {
    metl::fixed_vector<int, 4> v;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      v.pop_back();
    }
    CHECK(g_fired);
    CHECK_EQ(v.size(), 0u);

    metl::fixed_stack<int, 4> s;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      s.pop();
    }
    CHECK(g_fired);
    CHECK_EQ(s.size(), 0u);
  }

  // A non-power-of-two alignment.
  {
    metl::monotonic_buffer<64> buffer;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      (void)buffer.allocate(4, 3);
    }
    CHECK(g_fired);
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      (void)buffer.allocate(4, 0);
    }
    CHECK(g_fired);
    g_fired = false;
    void* ok = buffer.allocate(4, 4);
    CHECK(!g_fired);
    CHECK(ok != nullptr);
  }

  // Empty pops on the ring containers (G.6): destroyed a dead slot and wrapped
  // size_ to SIZE_MAX; a fixed_queue destructor then looped ~2^64 times.
  {
    metl::ring_buffer<int, 4> ring;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      ring.pop_front();
    }
    CHECK(g_fired);
    CHECK_EQ(ring.size(), 0u);

    metl::fixed_deque<int, 4> deque;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      deque.pop_back();
    }
    CHECK(g_fired);
    CHECK_EQ(deque.size(), 0u);

    metl::fixed_queue<int, 4> queue;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      queue.pop();
    }
    CHECK(g_fired);
    CHECK_EQ(queue.size(), 0u);

    metl::static_message_queue<int, 4> messages;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      messages.pop();
    }
    CHECK(g_fired);
    CHECK_EQ(messages.size(), 0u);
  }

  return metl_test::exit_code();
}
