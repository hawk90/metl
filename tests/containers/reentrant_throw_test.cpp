// A constructor that inserts into its own container and then throws (#638).
//
// The containers claim a slot before running T's constructor, so a
// constructor that pushes into the same container gets the next slot, and
// they undo the claim if the constructor throws. Doing both in one insert used
// to leave the slot whose constructor never finished counted as live, and the
// element the constructor inserted outside the live range. The undo now stops
// at a METL_HARDEN instead: the claim cannot be taken back without moving the
// inner element, and METL does not move elements behind the caller's back.
//
// The handler longjmps out of the undo, which runs while the exception
// unwinds. Each container is then abandoned (namespace-scope storage that is
// never destroyed), so nothing touches the state the trip interrupted. The
// exception in flight is abandoned with it: nothing can free it once the
// handler jumps out of the unwind, so LeakSanitizer is told about that one
// allocation, and only that one.

#undef METL_HARDENING  // METL_HARDEN must fire at every level, including NONE
#define METL_HARDENING 0

#include "metl_check.hpp"
#include "metl_deprecated.hpp"  // still tests metl::variant / static_message_queue (#645)

#include <csetjmp>
#include <new>

#include <metl/assert.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_message_queue.hpp>

#if METL_NO_EXCEPTIONS
int main() {
  return metl_test::skip("reentrant_throw_test", "needs exceptions");
}
#else

// Read by LeakSanitizer when the test runs under it; unused anywhere else.
extern "C" const char* __lsan_default_suppressions();
extern "C" const char* __lsan_default_suppressions() {
  return "leak:inner_then_throw::inner_then_throw\n";
}

namespace {

std::jmp_buf g_jump;
bool g_fired = false;

void capture(const char* /*expr*/, const char* /*file*/, int /*line*/) noexcept {
  g_fired = true;
  std::longjmp(g_jump, 1);
}

struct inner_then_throw;
void (*g_reenter)() = nullptr;

// Constructing with `true` inserts another element into the same container,
// then throws.
struct inner_then_throw {
  int value;
  explicit inner_then_throw(int v) : value(v) {}
  inner_then_throw(int v, bool reenter) : value(v) {
    if (reenter) {
      g_reenter();
      throw 1;
    }
  }
};

// Storage that is never destroyed: the trip leaves each container mid-insert.
template <typename C>
C& abandoned() {
  alignas(C) static unsigned char bytes[sizeof(C)];
  return *::new (static_cast<void*>(bytes)) C();
}

template <typename F>
bool trips(F insert) {
  g_fired = false;
  if (setjmp(g_jump) == 0) {
    try {
      insert();
    } catch (int) {
    }
  }
  return g_fired;
}

using vector_t = metl::fixed_vector<inner_then_throw, 4>;
using deque_t = metl::fixed_deque<inner_then_throw, 4>;
using ring_t = metl::ring_buffer<inner_then_throw, 4>;
using queue_t = metl::static_message_queue<inner_then_throw, 4>;

vector_t* g_vector = nullptr;
deque_t* g_deque = nullptr;
ring_t* g_ring = nullptr;
queue_t* g_queue = nullptr;

}  // namespace

int main() {
  (void)metl::set_assert_handler(&capture);

  // A throw with no reentrant insert is still undone, at every level.
  {
    static vector_t plain;
    try {
      (void)plain.try_emplace_back(1, false);
      g_reenter = [] {};
      (void)plain.try_emplace_back(2, true);
    } catch (int) {
    }
    CHECK_EQ(plain.size(), 1u);
    g_reenter = nullptr;
  }

  g_vector = &abandoned<vector_t>();
  g_reenter = [] { g_vector->emplace_back(99); };
  CHECK(trips([] { (void)g_vector->try_emplace_back(2, true); }));

  g_deque = &abandoned<deque_t>();
  g_reenter = [] { g_deque->emplace_front(99); };
  CHECK(trips([] { (void)g_deque->try_emplace_front(2, true); }));

  g_deque = &abandoned<deque_t>();
  g_reenter = [] { g_deque->emplace_back(99); };
  CHECK(trips([] { (void)g_deque->try_emplace_back(2, true); }));

  g_ring = &abandoned<ring_t>();
  g_reenter = [] { (void)g_ring->try_push_back(inner_then_throw(99)); };
  CHECK(trips([] { (void)g_ring->try_emplace_back(2, true); }));

  g_queue = &abandoned<queue_t>();
  g_reenter = [] { (void)g_queue->try_emplace(99); };
  CHECK(trips([] { (void)g_queue->try_emplace(2, true); }));

  return metl_test::exit_code();
}

#endif
