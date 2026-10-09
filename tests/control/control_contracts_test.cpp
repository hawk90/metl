// Contract guarantees stated in the headers that no other test would catch
// breaking:
//
//   * event_dispatcher::subscribe on a full dispatcher returns nullopt and
//     leaves the existing subscriptions working.
//   * deadline_scheduler::try_schedule holds one slot back only while a poll
//     is on the stack; otherwise it accepts exactly Capacity entries.
//   * scheduler::run_once does not poll a task another task's poll detached
//     earlier in the same round.
//   * variant::emplace with a throwing constructor leaves the variant unchanged.
//   * fixed_priority_queue clears itself when the comparator throws, and the
//     exception reaches the caller.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/config.hpp>
#include <metl/coro/deadline_scheduler.hpp>
#include <metl/coro/scheduler.hpp>
#include <metl/delegate.hpp>
#include <metl/event_dispatcher.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/variant.hpp>

namespace {

// --- event_dispatcher ---------------------------------------------------------

struct counter {
  int calls = 0;
  int last = 0;
  void on_event(int value) noexcept {
    ++calls;
    last = value;
  }
};

void test_dispatcher_full_subscribe() {
  using dispatcher_type = metl::event_dispatcher<void(int), 2>;
  using delegate_type = dispatcher_type::delegate_type;

  dispatcher_type dispatcher;
  counter a;
  counter b;
  counter c;

  const auto id_a = dispatcher.subscribe(delegate_type::bind<counter, &counter::on_event>(a));
  const auto id_b = dispatcher.subscribe(delegate_type::bind<counter, &counter::on_event>(b));
  if (!CHECK(id_a.has_value() && id_b.has_value())) {
    return;
  }

  // Full: refused, and nothing already there is displaced.
  CHECK(!dispatcher.subscribe(delegate_type::bind<counter, &counter::on_event>(c)).has_value());
  CHECK_EQ(dispatcher.size(), std::size_t{2});

  dispatcher.dispatch(7);
  CHECK_EQ(a.calls, 1);
  CHECK_EQ(a.last, 7);
  CHECK_EQ(b.calls, 1);
  CHECK_EQ(b.last, 7);
  CHECK_EQ(c.calls, 0);

  // The ids issued before the refusal still name their listeners.
  CHECK(dispatcher.unsubscribe(id_a.value()));
  dispatcher.dispatch(8);
  CHECK_EQ(a.calls, 1);
  CHECK_EQ(b.calls, 2);
  CHECK_EQ(b.last, 8);
  CHECK(dispatcher.unsubscribe(id_b.value()));
  CHECK(dispatcher.empty());
}

// --- coro::deadline_scheduler -------------------------------------------------

using tick = std::uint32_t;
constexpr std::size_t kDeadlineCapacity = 4;
using deadline_type = metl::coro::deadline_scheduler<kDeadlineCapacity, tick>;

std::size_t g_accepted_in_poll = 0;
int g_dummy_task = 0;

metl::optional<tick> idle_poll(void*, tick) noexcept {
  return metl::nullopt;
}

// Schedules as many entries as the scheduler will take while its own poll is
// on the stack, then finishes without re-arming.
// Function-local, not a stack object behind a global pointer: CodeQL flags a
// stack address stored in non-local memory.
deadline_type& reserve_sched() {
  static deadline_type sched;
  return sched;
}

metl::optional<tick> filling_poll(void*, tick) noexcept {
  while (reserve_sched().try_schedule(&g_dummy_task, &idle_poll, tick{100})) {
    ++g_accepted_in_poll;
  }
  return metl::nullopt;
}

void test_deadline_reserve_only_during_poll() {
  // Outside any poll: exactly Capacity entries fit.
  {
    deadline_type sched;
    std::size_t accepted = 0;
    for (std::size_t i = 0; i < kDeadlineCapacity + 2; ++i) {
      if (sched.try_schedule(&g_dummy_task, &idle_poll, tick{100})) {
        ++accepted;
      }
    }
    CHECK_EQ(accepted, kDeadlineCapacity);
    CHECK_EQ(sched.task_count(), kDeadlineCapacity);
  }

  // Inside a poll: the running task's entry is popped (one slot free) and one
  // slot is held back for its re-arm, so try_schedule says full one early.
  {
    deadline_type& sched = reserve_sched();
    sched.clear();
    g_accepted_in_poll = 0;
    static int filler = 0;
    CHECK(sched.try_schedule(&filler, &filling_poll, tick{0}));
    CHECK_EQ(sched.run_due(tick{0}), std::size_t{1});
    CHECK_EQ(g_accepted_in_poll, kDeadlineCapacity - 1);
    CHECK_EQ(sched.task_count(), kDeadlineCapacity - 1);

    // The poll has returned: the reserved slot is released again.
    CHECK(sched.try_schedule(&g_dummy_task, &idle_poll, tick{100}));
    CHECK(!sched.try_schedule(&g_dummy_task, &idle_poll, tick{100}));
    CHECK_EQ(sched.task_count(), kDeadlineCapacity);
  }
}

// --- coro::scheduler ----------------------------------------------------------

using round_type = metl::coro::scheduler<4>;

round_type& round_sched() {
  static round_type sched;
  return sched;
}
int g_victim_polls = 0;
int g_detacher_polls = 0;
int g_victim_task = 0;

bool victim_poll(void*) noexcept {
  ++g_victim_polls;
  return true;
}

bool detacher_poll(void*) noexcept {
  ++g_detacher_polls;
  CHECK(round_sched().detach(&g_victim_task));
  return false;
}

void test_scheduler_skips_detached_in_round() {
  round_type& sched = round_sched();
  static int detacher_task = 0;

  // The detacher runs first in attachment order and removes the victim, which
  // was in the round's snapshot.
  CHECK(sched.try_attach(&detacher_task, &detacher_poll));
  CHECK(sched.try_attach(&g_victim_task, &victim_poll));

  CHECK_EQ(sched.run_once(), std::size_t{0});
  CHECK_EQ(g_detacher_polls, 1);
  CHECK_EQ(g_victim_polls, 0);
  CHECK(sched.empty());
}

#if !METL_NO_EXCEPTIONS

// --- variant::emplace ---------------------------------------------------------

struct throw_tag {};

struct fragile {
  int value;
  explicit fragile(int v) : value(v) {}
  explicit fragile(throw_tag) : value(0) { throw 42; }
  fragile(const fragile&) = default;
  fragile(fragile&&) noexcept = default;
};

void test_variant_emplace_throw_unchanged() {
  // From another alternative.
  {
    metl::variant<int, fragile> v(5);
    bool thrown = false;
    try {
      v.emplace<fragile>(throw_tag{});
    } catch (int) {
      thrown = true;
    }
    CHECK(thrown);
    CHECK(!v.valueless_by_exception());
    CHECK_EQ(v.index(), std::size_t{0});
    if (const int* held = metl::get_if<int>(&v); CHECK(held != nullptr)) {
      CHECK_EQ(*held, 5);
    }
  }
  // Over the same alternative, by index.
  {
    metl::variant<int, fragile> v(fragile(9));
    bool thrown = false;
    try {
      v.emplace<1>(throw_tag{});
    } catch (int) {
      thrown = true;
    }
    CHECK(thrown);
    CHECK_EQ(v.index(), std::size_t{1});
    if (const fragile* held = metl::get_if<fragile>(&v); CHECK(held != nullptr)) {
      CHECK_EQ(held->value, 9);
    }
  }
}

// --- fixed_priority_queue -----------------------------------------------------

bool g_compare_throws = false;

struct throwing_less {
  bool operator()(int lhs, int rhs) const {
    if (g_compare_throws) {
      throw 7;
    }
    return lhs < rhs;
  }
};

using pq_type = metl::fixed_priority_queue<int, 8, throwing_less>;

void fill(pq_type& q) {
  g_compare_throws = false;
  q.clear();
  for (int i = 1; i <= 4; ++i) {
    q.push(i);
  }
}

template <typename Op>
void expect_cleared_and_rethrown(Op op) {
  pq_type q;
  fill(q);
  g_compare_throws = true;
  bool thrown = false;
  try {
    op(q);
  } catch (int code) {
    thrown = true;
    CHECK_EQ(code, 7);
  }
  g_compare_throws = false;
  CHECK(thrown);
  CHECK(q.empty());
  CHECK_EQ(q.size(), std::size_t{0});
  // Still usable afterwards.
  q.push(3);
  q.push(5);
  CHECK_EQ(q.top(), 5);
}

void test_priority_queue_comparator_throw() {
  expect_cleared_and_rethrown([](pq_type& q) { q.push(10); });
  expect_cleared_and_rethrown([](pq_type& q) { (void)q.try_push(10); });
  expect_cleared_and_rethrown([](pq_type& q) { q.pop(); });
  expect_cleared_and_rethrown([](pq_type& q) { (void)q.erase_if([](int x) { return x == 1; }); });
}

#endif  // !METL_NO_EXCEPTIONS

}  // namespace

int main() {
  test_dispatcher_full_subscribe();
  test_deadline_reserve_only_during_poll();
  test_scheduler_skips_detached_in_round();
#if !METL_NO_EXCEPTIONS
  test_variant_emplace_throw_unchanged();
  test_priority_queue_comparator_throw();
#endif
  return metl_test::exit_code();
}
