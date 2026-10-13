// Paths each type documents but no other test takes: a stepper polled again
// after it failed, a message queue popped while empty and filled past its last
// slot, an arena asked for an alignment its storage cannot reach, and the
// assert and panic handlers reset to their defaults with null.

#include "metl_check.hpp"

#include <cstdint>

#include <metl/arena_allocator.hpp>
#include <metl/assert.hpp>
#include <metl/coro/stepper.hpp>
#include <metl/static_message_queue.hpp>

namespace {

// ---- stepper -------------------------------------------------------------------

// Fails on its first step. That finishes the task, with an error, and poll()
// never calls step() again.
class failing : public metl::coro::stepper {
 public:
  int steps = 0;
  metl::coro::step_result step() noexcept override {
    ++steps;
    return metl::coro::step_result::error;
  }
};

void stepper_stays_failed() {
  failing task;
  CHECK(!task.poll());
  CHECK(task.is_error());
  CHECK(task.is_done());
  CHECK(!task.poll());
  CHECK_EQ(task.steps, 1);
}

// ---- static_message_queue ----------------------------------------------------------

void message_queue_pop_empty_and_wrap() {
  metl::static_message_queue<int, 3> queue;
  int out = -1;
  CHECK(!queue.try_pop(out));
  CHECK_EQ(out, -1);

  // Push three, pop two and push two more: the tail wraps past the last slot,
  // and each push still returns the element it built.
  queue.push(1);
  queue.push(2);
  queue.push(3);
  CHECK(queue.try_pop(out));
  CHECK(queue.try_pop(out));
  CHECK_EQ(queue.push(4), 4);
  CHECK_EQ(queue.push(5), 5);
  CHECK_EQ(queue.size(), std::size_t{3});
  for (const int expected : {3, 4, 5}) {
    CHECK(queue.try_pop(out));
    CHECK_EQ(out, expected);
  }
  CHECK(!queue.try_pop(out));
}

// ---- arena_allocator --------------------------------------------------------------------

// Aligning the first allocation to 256 bytes needs more padding than the arena
// has bytes: storage_ starts a few bytes into an object aligned to 256. The
// request fails and uses nothing.
void arena_refuses_padding_beyond_its_space() {
  alignas(256) metl::arena_allocator<64> arena;
  CHECK(arena.allocate(1, 256) == nullptr);
  CHECK_EQ(arena.used(), std::size_t{0});
  CHECK(arena.allocate(1, 1) != nullptr);
}

// ---- assert and panic handlers ---------------------------------------------------------------

void on_assert(const char*, const char*, int) noexcept {}
void on_panic(const char*, const char*, int) noexcept {}

// Null restores the default: the next call hands the default handler back.
void null_handler_restores_the_default() {
  const metl::assert_handler_t original_assert = metl::set_assert_handler(&on_assert);
  CHECK(metl::set_assert_handler(nullptr) == &on_assert);
  CHECK(metl::set_assert_handler(original_assert) == &metl::detail::default_assert_handler);

  const metl::panic_handler_t original_panic = metl::set_panic_handler(&on_panic);
  CHECK(metl::set_panic_handler(nullptr) == &on_panic);
  CHECK(metl::set_panic_handler(original_panic) == &metl::detail::default_panic_handler);
}

}  // namespace

int main() {
  stepper_stays_failed();
  message_queue_pop_empty_and_wrap();
  arena_refuses_padding_beyond_its_space();
  null_handler_restores_the_default();
  return metl_test::exit_code();
}
