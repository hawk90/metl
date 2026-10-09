// A failed try_* leaves the container exactly as it was.
//
// The recoverable API promises more than a false / null return: the call that
// fails must not have touched the object. recoverable_api_test checks the
// return values; nothing checked the state afterwards, so a try_* that
// claimed a slot, rotated the ring, released the old target or overwrote a
// record before noticing it had no room would still pass. Each function here
// fills the object, makes the failing call, and then reads the whole state
// back -- every element in order, used(), the destructors a reset runs -- and
// compares it with what was there before.
//
//   * fixed_function / fixed_any_invocable: try_assign of a callable larger
//     than Capacity keeps the ENGAGED target (the empty case is covered in
//     fixed_function_test).
//   * fixed_deque: try_push_front / try_emplace_front on a full deque.
//   * arena_allocator: a failed try_emplace leaves used() and the record chain
//     -- observed through what reset() destroys, and in which order.
//   * flat_set / static_unordered_set: try_emplace of a NEW key when full.
//   * static_message_queue, ring_buffer, fixed_queue, fixed_stack: a full
//     try_push / try_emplace.
//
// The rings are wrapped before they are filled (head not at slot 0), so a
// failure that writes through the wrong index cannot hide in a ring that
// happens to start at the beginning of its storage.

#include "metl_check.hpp"

#include <cstddef>

#include <metl/arena_allocator.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_function.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/flat_set.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

// --- fixed_function / fixed_any_invocable ------------------------------------

struct too_large {
  char payload[64];
  int operator()(int value) const noexcept { return value + payload[0]; }
};

struct adds {
  int amount;
  int operator()(int value) const noexcept { return value + amount; }
};

void fixed_function_failed_assign_keeps_target() {
  metl::fixed_function<int(int), 16> f(adds{41});
  CHECK(!f.try_assign(too_large{}));
  CHECK(f.has_value());
  if (f.has_value()) {
    CHECK_EQ(f(1), 42);
  }

  metl::fixed_function<int(int) noexcept, 16> g(adds{41});
  CHECK(!g.try_assign(too_large{}));
  CHECK(g.has_value());
  if (g.has_value()) {
    CHECK_EQ(g(1), 42);
  }
}

void fixed_any_invocable_failed_assign_keeps_target() {
  metl::fixed_any_invocable<int(int), 16> f(adds{41});
  CHECK(!f.try_assign(too_large{}));
  CHECK(f.has_value());
  if (f.has_value()) {
    CHECK_EQ(f(1), 42);
  }

  metl::fixed_any_invocable<int(int) noexcept, 16> g(adds{41});
  CHECK(!g.try_assign(too_large{}));
  CHECK(g.has_value());
  if (g.has_value()) {
    CHECK_EQ(g(1), 42);
  }
}

// --- fixed_deque --------------------------------------------------------------

// Fills a 4-slot deque with 1 2 3 4, wrapped so head_ is not slot 0.
void fill_deque(metl::fixed_deque<int, 4>& d) {
  d.clear();
  d.push_back(0);
  d.push_back(0);
  d.pop_front();
  d.pop_front();
  d.push_back(1);
  d.push_back(2);
  d.push_back(3);
  d.push_back(4);
}

void check_deque_is_1234(metl::fixed_deque<int, 4>& d) {
  CHECK(d.full());
  CHECK_EQ(d.size(), 4u);
  for (int expected = 1; expected <= 4 && !d.empty(); ++expected) {
    CHECK_EQ(d.front(), expected);
    d.pop_front();
  }
  CHECK(d.empty());
}

void fixed_deque_full_front_unchanged() {
  metl::fixed_deque<int, 4> d;
  const int value = 9;

  fill_deque(d);
  CHECK(!d.try_push_front(value));
  check_deque_is_1234(d);

  fill_deque(d);
  CHECK(!d.try_push_front(9));
  check_deque_is_1234(d);

  fill_deque(d);
  CHECK(!d.try_emplace_front(9));
  check_deque_is_1234(d);
}

// --- arena_allocator ----------------------------------------------------------

int g_destroy_log[8];
int g_destroy_count = 0;

struct logged {
  int id;
  explicit logged(int i) : id(i) {}
  logged(const logged&) = delete;
  logged& operator=(const logged&) = delete;
  ~logged() {
    if (g_destroy_count < 8) {
      g_destroy_log[g_destroy_count] = id;
    }
    ++g_destroy_count;
  }
};

struct huge {
  unsigned char bytes[1024];
};

void arena_failed_emplace_unchanged() {
  g_destroy_count = 0;
  {
    metl::arena_allocator<256> arena;
    CHECK(arena.try_emplace<logged>(1) != nullptr);
    CHECK(arena.try_emplace<logged>(2) != nullptr);
    const std::size_t used_before = arena.used();

    CHECK(arena.try_emplace<huge>() == nullptr);
    CHECK_EQ(arena.used(), used_before);
    CHECK_EQ(arena.remaining(), arena.capacity() - used_before);
    CHECK_EQ(g_destroy_count, 0);

    // The chain still ends at object 2: a later allocation stacks on it, and
    // reset destroys all three, newest first.
    logged* third = arena.try_emplace<logged>(3);
    CHECK(third != nullptr);
    CHECK(arena.used() > used_before);
    arena.reset();
    CHECK_EQ(arena.used(), 0u);
    CHECK_EQ(g_destroy_count, 3);
    if (g_destroy_count == 3) {
      CHECK_EQ(g_destroy_log[0], 3);
      CHECK_EQ(g_destroy_log[1], 2);
      CHECK_EQ(g_destroy_log[2], 1);
    }
  }
  CHECK_EQ(g_destroy_count, 3);

  // A failure right after a mark: rewinding to that mark still destroys
  // exactly what came after it.
  g_destroy_count = 0;
  {
    metl::arena_allocator<256> arena;
    CHECK(arena.try_emplace<logged>(1) != nullptr);
    const auto mark = arena.mark();
    CHECK(arena.try_emplace<logged>(2) != nullptr);
    CHECK(arena.try_emplace<huge>() == nullptr);
    arena.rewind(mark);
    CHECK_EQ(arena.used(), mark.offset);
    CHECK_EQ(g_destroy_count, 1);
    if (g_destroy_count == 1) {
      CHECK_EQ(g_destroy_log[0], 2);
    }
  }
  CHECK_EQ(g_destroy_count, 2);
}

// --- flat_set / static_unordered_set -----------------------------------------

void flat_set_full_new_key_unchanged() {
  metl::flat_set<int, 4> s;
  CHECK(s.try_emplace(10));
  CHECK(s.try_emplace(20));
  CHECK(s.try_emplace(30));
  CHECK(s.try_emplace(40));
  CHECK(s.full());

  // New keys that would land at the front, the middle and the back.
  CHECK(!s.try_emplace(5));
  CHECK(!s.try_emplace(25));
  CHECK(!s.try_emplace(50));

  CHECK_EQ(s.size(), 4u);
  const int expected[] = {10, 20, 30, 40};
  std::size_t i = 0;
  for (const int key : s) {
    if (i < 4) {
      CHECK_EQ(key, expected[i]);
    }
    ++i;
  }
  CHECK_EQ(i, 4u);
  CHECK(!s.contains(5));
  CHECK(!s.contains(25));
  CHECK(!s.contains(50));
}

void static_unordered_set_full_new_key_unchanged() {
  metl::static_unordered_set<int, 4> s;
  CHECK(s.try_emplace(10));
  CHECK(s.try_emplace(20));
  CHECK(s.try_emplace(30));
  CHECK(s.try_emplace(40));
  CHECK(s.full());

  CHECK(!s.try_emplace(5));
  CHECK(!s.try_emplace(25));
  CHECK(!s.try_emplace(50));

  CHECK_EQ(s.size(), 4u);
  std::size_t seen = 0;
  int sum = 0;
  for (const int key : s) {
    ++seen;
    sum += key;
  }
  CHECK_EQ(seen, 4u);
  CHECK_EQ(sum, 100);
  CHECK(s.contains(10));
  CHECK(s.contains(20));
  CHECK(s.contains(30));
  CHECK(s.contains(40));
  CHECK(!s.contains(5));
  CHECK(!s.contains(25));
  CHECK(!s.contains(50));
}

// --- FIFO / LIFO adaptors -----------------------------------------------------

void static_message_queue_full_unchanged() {
  metl::static_message_queue<int, 4> q;
  q.push(0);
  q.push(0);
  int sink = 0;
  CHECK(q.try_pop(sink));
  CHECK(q.try_pop(sink));
  q.push(1);
  q.push(2);
  q.push(3);
  q.push(4);
  CHECK(q.full());

  const int value = 9;
  CHECK(!q.try_push(value));
  CHECK(!q.try_push(9));
  CHECK(!q.try_emplace(9));

  CHECK_EQ(q.size(), 4u);
  for (int expected = 1; expected <= 4; ++expected) {
    int out = 0;
    CHECK(q.try_pop(out));
    CHECK_EQ(out, expected);
  }
  CHECK(q.empty());
}

void ring_buffer_full_unchanged() {
  metl::ring_buffer<int, 4> r;
  r.emplace_back(0);
  r.emplace_back(0);
  r.pop_front();
  r.pop_front();
  r.emplace_back(1);
  r.emplace_back(2);
  r.emplace_back(3);
  r.emplace_back(4);
  CHECK(r.full());

  const int value = 9;
  CHECK(!r.try_push_back(value));
  CHECK(!r.try_push_back(9));
  CHECK(!r.try_emplace_back(9));

  CHECK_EQ(r.size(), 4u);
  for (int expected = 1; expected <= 4 && !r.empty(); ++expected) {
    CHECK_EQ(r.front(), expected);
    r.pop_front();
  }
  CHECK(r.empty());
}

void fixed_queue_full_unchanged() {
  metl::fixed_queue<int, 4> q;
  q.push(0);
  q.push(0);
  q.pop();
  q.pop();
  q.push(1);
  q.push(2);
  q.push(3);
  q.push(4);
  CHECK(q.full());

  const int value = 9;
  CHECK(!q.try_push(value));
  CHECK(!q.try_push(9));
  CHECK(!q.try_emplace(9));

  CHECK_EQ(q.size(), 4u);
  for (int expected = 1; expected <= 4 && !q.empty(); ++expected) {
    CHECK_EQ(q.front(), expected);
    q.pop();
  }
  CHECK(q.empty());
}

void fixed_stack_full_unchanged() {
  metl::fixed_stack<int, 4> s;
  s.push(1);
  s.push(2);
  s.push(3);
  s.push(4);
  CHECK(s.full());

  const int value = 9;
  CHECK(!s.try_push(value));
  CHECK(!s.try_push(9));
  CHECK(!s.try_emplace(9));

  CHECK_EQ(s.size(), 4u);
  for (int expected = 4; expected >= 1 && !s.empty(); --expected) {
    CHECK_EQ(s.top(), expected);
    s.pop();
  }
  CHECK(s.empty());
}

}  // namespace

int main() {
  fixed_function_failed_assign_keeps_target();
  fixed_any_invocable_failed_assign_keeps_target();
  fixed_deque_full_front_unchanged();
  arena_failed_emplace_unchanged();
  flat_set_full_new_key_unchanged();
  static_unordered_set_full_new_key_unchanged();
  static_message_queue_full_unchanged();
  ring_buffer_full_unchanged();
  fixed_queue_full_unchanged();
  fixed_stack_full_unchanged();
  return metl_test::exit_code();
}
