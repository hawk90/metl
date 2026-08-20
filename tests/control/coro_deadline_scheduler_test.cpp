#include "metl_check.hpp"

#include "metl/coro/deadline_scheduler.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using tick = std::uint32_t;
using scheduler_type = metl::coro::deadline_scheduler<8, tick>;

/// Records the order tasks were polled in, so "earliest deadline first" is
/// checked against observed dispatch rather than inferred from the queue.
constexpr std::size_t kLogCapacity = 32;
int g_log[kLogCapacity];
std::size_t g_log_size = 0;

void record(int id) noexcept {
  if (g_log_size < kLogCapacity) {
    g_log[g_log_size] = id;
    ++g_log_size;
  }
}

void reset_log() noexcept {
  g_log_size = 0;
}

/// A task that runs once and is done.
struct one_shot {
  int id;

  static metl::optional<tick> poll(void* self, tick) noexcept {
    record(static_cast<one_shot*>(self)->id);
    return metl::nullopt;
  }
};

/// A task that re-arms itself `remaining` more times, `period` ticks apart.
struct periodic {
  int id;
  tick period;
  int remaining;

  static metl::optional<tick> poll(void* self, tick now) noexcept {
    auto* task = static_cast<periodic*>(self);
    record(task->id);
    if (task->remaining <= 0) {
      return metl::nullopt;
    }
    --task->remaining;
    return now + task->period;
  }
};

/// Re-arms at a deadline that is already due, which is what `max_dispatches`
/// exists to bound.
struct immediate_rearm {
  static metl::optional<tick> poll(void*, tick now) noexcept {
    record(0);
    return now;  // still due: asks to run again inside this same run_due
  }
};

/// Fills the scheduler from inside its own poll, to exercise the reserved slot.
struct greedy {
  scheduler_type* owner;
  one_shot* filler;
  std::size_t filler_count;
  std::size_t accepted;

  static metl::optional<tick> poll(void* self, tick now) noexcept {
    auto* task = static_cast<greedy*>(self);
    record(1);
    for (std::size_t i = 0; i < task->filler_count; ++i) {
      if (task->owner->try_schedule(&task->filler[i], &one_shot::poll, now + 100)) {
        ++task->accepted;
      }
    }
    return now + 10;  // must still be able to re-arm
  }
};

}  // namespace

int main() {
  // ---------------------------------------------------------------------
  // Earliest deadline first, regardless of scheduling order.
  // ---------------------------------------------------------------------
  {
    scheduler_type scheduler;
    one_shot a{10};
    one_shot b{20};
    one_shot c{30};

    CHECK(scheduler.try_schedule(&b, &one_shot::poll, 200));
    CHECK(scheduler.try_schedule(&c, &one_shot::poll, 300));
    CHECK(scheduler.try_schedule(&a, &one_shot::poll, 100));
    CHECK_EQ(scheduler.task_count(), 3u);

    const metl::optional<tick> next = scheduler.next_deadline();
    CHECK(next.has_value());
    CHECK_EQ(*next, 100u);

    // Nothing is due yet.
    reset_log();
    CHECK_EQ(scheduler.run_due(99), 0u);
    CHECK_EQ(g_log_size, 0u);
    CHECK_EQ(scheduler.task_count(), 3u);

    // A deadline exactly equal to `now` is due.
    CHECK_EQ(scheduler.run_due(100), 1u);
    CHECK_EQ(g_log_size, 1u);
    CHECK_EQ(g_log[0], 10);
    CHECK_EQ(scheduler.task_count(), 2u);

    // Everything remaining goes at once, still in deadline order.
    CHECK_EQ(scheduler.run_due(999), 2u);
    CHECK_EQ(g_log_size, 3u);
    CHECK_EQ(g_log[1], 20);
    CHECK_EQ(g_log[2], 30);
    CHECK(scheduler.empty());
    CHECK(!scheduler.next_deadline().has_value());
  }

  // ---------------------------------------------------------------------
  // A full queue drained in one call, with the deadlines scheduled in a
  // scrambled order.
  //
  // Added because the three-task case above does NOT distinguish a working
  // sift_down from a broken one: with a shallow heap, moving the last element
  // to the root and skipping the sift still happens to answer correctly.
  // Mutating `sift_down` to a no-op at the root leaves that case green and this
  // one red, which is the only reason to prefer eight tasks to three.
  // ---------------------------------------------------------------------
  {
    scheduler_type scheduler;
    // task[i] runs i-th, so the expected dispatch log is simply 0..7.
    const tick deadlines[8] = {50, 10, 80, 30, 70, 20, 60, 40};
    const int ids[8] = {4, 0, 7, 2, 6, 1, 5, 3};
    one_shot tasks[8] = {{ids[0]}, {ids[1]}, {ids[2]}, {ids[3]}, {ids[4]}, {ids[5]}, {ids[6]}, {ids[7]}};
    for (std::size_t i = 0; i < 8; ++i) {
      CHECK(scheduler.try_schedule(&tasks[i], &one_shot::poll, deadlines[i]));
    }
    CHECK_EQ(scheduler.task_count(), 8u);

    reset_log();
    CHECK_EQ(scheduler.run_due(999), 8u);
    CHECK(scheduler.empty());
    CHECK_EQ(g_log_size, 8u);
    for (std::size_t i = 0; i < 8; ++i) {
      CHECK_EQ(g_log[i], static_cast<int>(i));
    }
  }

  // ---------------------------------------------------------------------
  // A task that returns a next deadline is re-armed; nullopt retires it.
  // ---------------------------------------------------------------------
  {
    scheduler_type scheduler;
    periodic ticker{7, 50, 2};  // runs at 100, 150, 200, then retires

    scheduler.schedule(&ticker, &periodic::poll, 100);
    reset_log();

    CHECK_EQ(scheduler.run_due(100), 1u);
    CHECK_EQ(scheduler.task_count(), 1u);
    const metl::optional<tick> after_first = scheduler.next_deadline();
    CHECK(after_first.has_value());
    CHECK_EQ(*after_first, 150u);

    CHECK_EQ(scheduler.run_due(149), 0u);
    CHECK_EQ(scheduler.run_due(150), 1u);
    CHECK_EQ(scheduler.run_due(200), 1u);
    // remaining hit 0 on that third poll, so it retired.
    CHECK(scheduler.empty());
    CHECK_EQ(g_log_size, 3u);
    CHECK_EQ(g_log[0], 7);
    CHECK_EQ(g_log[2], 7);
  }

  // ---------------------------------------------------------------------
  // max_dispatches bounds a task that keeps re-arming as already-due.
  // Without it, run_due would not terminate.
  // ---------------------------------------------------------------------
  {
    scheduler_type scheduler;
    int spinner = 0;
    scheduler.schedule(&spinner, &immediate_rearm::poll, 0);

    reset_log();
    CHECK_EQ(scheduler.run_due(1000, 5), 5u);
    CHECK_EQ(g_log_size, 5u);
    // Still scheduled and still due: the bound stopped the loop, it did not
    // silently drop the task.
    CHECK_EQ(scheduler.task_count(), 1u);
    const metl::optional<tick> still = scheduler.next_deadline();
    CHECK(still.has_value());
    CHECK_EQ(*still, 1000u);
  }

  // ---------------------------------------------------------------------
  // cancel() removes pending entries and answers whether there were any.
  // ---------------------------------------------------------------------
  {
    scheduler_type scheduler;
    one_shot a{1};
    one_shot b{2};

    scheduler.schedule(&a, &one_shot::poll, 100);
    scheduler.schedule(&b, &one_shot::poll, 200);
    scheduler.schedule(&a, &one_shot::poll, 300);  // same task, twice
    CHECK_EQ(scheduler.task_count(), 3u);
    CHECK(scheduler.is_scheduled(&a));

    CHECK(scheduler.cancel(&a));  // removes both of a's entries
    CHECK_EQ(scheduler.task_count(), 1u);
    CHECK(!scheduler.is_scheduled(&a));
    CHECK(scheduler.is_scheduled(&b));
    CHECK(!scheduler.cancel(&a));  // nothing left to cancel

    // The heap still works after the rebuild erase_if does.
    const metl::optional<tick> next = scheduler.next_deadline();
    CHECK(next.has_value());
    CHECK_EQ(*next, 200u);

    reset_log();
    CHECK_EQ(scheduler.run_due(999), 1u);
    CHECK_EQ(g_log_size, 1u);
    CHECK_EQ(g_log[0], 2);
  }

  // ---------------------------------------------------------------------
  // Full: try_schedule refuses, schedule() would assert (not exercised here).
  // ---------------------------------------------------------------------
  {
    scheduler_type scheduler;
    one_shot tasks[9] = {{0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}};
    for (std::size_t i = 0; i < 8; ++i) {
      CHECK(scheduler.try_schedule(&tasks[i], &one_shot::poll, static_cast<tick>(i)));
    }
    CHECK_EQ(scheduler.task_count(), 8u);
    CHECK(!scheduler.try_schedule(&tasks[8], &one_shot::poll, 0));
    CHECK_EQ(scheduler.task_count(), 8u);

    scheduler.clear();
    CHECK(scheduler.empty());
  }

  // ---------------------------------------------------------------------
  // Reentrancy: a poll may schedule, and the running task's re-arm is still
  // guaranteed a slot. try_schedule reports full one slot early instead.
  // ---------------------------------------------------------------------
  {
    scheduler_type scheduler;
    one_shot fillers[8] = {{0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}};
    greedy hog{&scheduler, fillers, 8, 0};

    scheduler.schedule(&hog, &greedy::poll, 10);
    reset_log();
    // The greedy task is popped (queue empty), then tries to add 8. Capacity is
    // 8 but one slot is reserved for its own re-arm, so exactly 7 are accepted.
    CHECK_EQ(scheduler.run_due(10, 1), 1u);
    CHECK_EQ(hog.accepted, 7u);
    // 7 fillers + the re-armed greedy task = 8, exactly full and not overflowed.
    CHECK_EQ(scheduler.task_count(), 8u);
    CHECK(scheduler.is_scheduled(&hog));

    const metl::optional<tick> next = scheduler.next_deadline();
    CHECK(next.has_value());
    CHECK_EQ(*next, 20u);  // the greedy re-arm at now + 10 beats the fillers at now + 100
  }

  return metl_test::exit_code();
}
