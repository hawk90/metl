// deadline_tasks.cpp
//
// Running periodic work by DEADLINE instead of polling everything every pass,
// with metl::coro::deadline_scheduler over metl::fixed_priority_queue.
//
// The round-robin `coro::scheduler` visits every attached task on every loop
// iteration, so a task that only wants to run in 500 ms still costs a call and
// has to check the clock itself. A deadline scheduler keeps tasks in a heap
// ordered by when they next want to run, and `run_due(now)` touches only the
// ones that are actually due. The loop can then sleep until `next_deadline()`
// instead of spinning.
//
// Modelled here with a fake millisecond clock so the example is deterministic
// and self-checking: it asserts the exact dispatch order and counts, and
// returns non-zero if any of it is wrong.
//
// NOTE ON THE CLOCK: `Tick` is compared with plain `<`, so it must not wrap.
// A rolling hardware counter has to be widened where the overflow is observed
// (accumulate into a software tick in the overflow ISR) before it is handed to
// the scheduler. Comparing signed differences instead is NOT a strict weak
// ordering across a full period, and a heap needs one -- it would misorder
// silently rather than fail.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <metl/coro/deadline_scheduler.hpp>

namespace {

using tick = std::uint32_t;
using scheduler_type = metl::coro::deadline_scheduler<8, tick>;

// A dispatch log, so the example can check the ORDER and not merely the count.
constexpr std::size_t kLogCapacity = 32;
char g_log[kLogCapacity + 1];
std::size_t g_log_size = 0;

void record(char id) noexcept {
  if (g_log_size < kLogCapacity) {
    g_log[g_log_size] = id;
    ++g_log_size;
    g_log[g_log_size] = '\0';
  }
}

// A periodic task: toggles an output every `period` ticks, forever.
struct heartbeat {
  char id;
  tick period;
  int toggles;
  bool led;

  static metl::optional<tick> poll(void* self, tick now) noexcept {
    auto* task = static_cast<heartbeat*>(self);
    task->led = !task->led;
    ++task->toggles;
    record(task->id);
    return now + task->period;  // re-arm
  }
};

// A one-shot timeout: runs once and retires by returning nullopt.
struct timeout {
  char id;
  bool fired;

  static metl::optional<tick> poll(void* self, tick) noexcept {
    auto* task = static_cast<timeout*>(self);
    task->fired = true;
    record(task->id);
    return metl::nullopt;  // done: do not reschedule
  }
};

}  // namespace

int main() {
  scheduler_type tasks;

  heartbeat fast{'f', 10, 0, false};
  heartbeat slow{'s', 25, 0, false};
  timeout deadline{'t', false};

  // try_schedule reports a full scheduler; schedule() asserts instead. Both are
  // available for every call -- see SCOPE.md section 9.
  if (!tasks.try_schedule(&fast, &heartbeat::poll, 10)) {
    return 1;
  }
  if (!tasks.try_schedule(&slow, &heartbeat::poll, 25)) {
    return 2;
  }
  if (!tasks.try_schedule(&deadline, &timeout::poll, 30)) {
    return 3;
  }

  // The idle loop an MCU would write: advance to the next deadline rather than
  // polling on every millisecond. On real hardware `sleep_until(*next)` would be
  // a WFI/WFE with the timer programmed to that tick.
  tick now = 0;
  std::size_t wakeups = 0;
  while (now < 60) {
    const metl::optional<tick> next = tasks.next_deadline();
    if (!next.has_value()) {
      break;  // nothing left to run
    }
    now = *next;  // "sleep" until the earliest deadline
    ++wakeups;
    if (tasks.run_due(now) == 0) {
      return 4;  // we woke for a deadline, so something must have been due
    }
  }

  // Expected timeline, earliest deadline first at every step:
  //   10 f | 20 f | 25 s | 30 f,t | 40 f | 50 f,s | 60 f
  // The tie at 50 (fast re-armed to 50, slow re-armed to 50) is broken in an
  // unspecified order -- the heap is not stable -- so only the SET at each tick
  // is checked below, not the order within a tick.
  static const char* const kExpectedPrefix = "ffsf";
  for (std::size_t i = 0; i < 4; ++i) {
    if (g_log[i] != kExpectedPrefix[i]) {
      std::printf("dispatch order wrong at %zu: got '%c', want '%c' (log=%s)\n",
                  i,
                  g_log[i],
                  kExpectedPrefix[i],
                  g_log);
      return 5;
    }
  }

  // The one-shot fired exactly once and retired; the heartbeats kept going.
  if (!deadline.fired) {
    return 6;
  }
  if (tasks.is_scheduled(&deadline)) {
    return 7;  // returning nullopt must remove it
  }
  if (fast.toggles != 6) {  // 10,20,30,40,50,60
    std::printf("fast ran %d times, expected 6 (log=%s)\n", fast.toggles, g_log);
    return 8;
  }
  if (slow.toggles != 2) {  // 25,50
    std::printf("slow ran %d times, expected 2 (log=%s)\n", slow.toggles, g_log);
    return 9;
  }

  // Waking only for real deadlines is the point: a 1 ms poll loop over 60 ticks
  // would have made 60 passes.
  if (wakeups > 8) {
    std::printf("woke %zu times, expected at most 8\n", wakeups);
    return 10;
  }

  // cancel() answers whether anything was pending, and takes the task out.
  if (!tasks.cancel(&slow)) {
    return 11;
  }
  if (tasks.cancel(&slow)) {
    return 12;  // already gone
  }
  if (tasks.is_scheduled(&slow)) {
    return 13;
  }

  std::printf("deadline_tasks: %zu wakeups, dispatch log = %s\n", wakeups, g_log);
  std::printf("  fast heartbeat: %d toggles, slow: %d, one-shot fired: %s\n",
              fast.toggles,
              slow.toggles,
              deadline.fired ? "yes" : "no");
  return 0;
}
