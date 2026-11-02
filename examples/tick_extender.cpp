// tick_extender.cpp
//
// A deadline that crosses a timer wrap, with metl::tick_extender.
//
// A 16-bit timer at 1 MHz wraps every 65 ms. Scheduling on its raw value breaks
// as soon as a deadline lies past the wrap: the deadline is a small number, the
// current count a large one, and `deadline_scheduler` compares them directly.
// `tick_extender<16>` keeps the wraps it has seen and returns a 64-bit tick
// that only grows, so deadlines can be scheduled on that instead.
//
// Modelled here with a fake 16-bit counter so the example is deterministic and
// self-checking: it advances the counter across several wraps, schedules a task
// whose deadlines straddle them, and returns non-zero if any dispatch lands at
// the wrong tick. On hardware the counter read is a register load, and an
// overflow interrupt calls `now()` too, so no wrap goes unseen (see the header).

#include <cstdint>
#include <cstdio>

#include <metl/coro/deadline_scheduler.hpp>
#include <metl/tick_extender.hpp>

namespace {

// The hardware timer: 16 bits, counting up, wrapping at 65536.
std::uint16_t g_counter = 0;

std::uint16_t read_timer() noexcept {
  return g_counter;
}

// The irq_lock default compiles to a compiler barrier on a hosted build, which
// is all a single-threaded example needs; on a Cortex-M it masks interrupts.
metl::tick_extender<16> g_ticks;

using tick = std::uint64_t;
metl::coro::deadline_scheduler<4, tick> g_tasks;

// A periodic task: runs every 20000 ticks and records when it ran.
constexpr tick kPeriod = 20000;
constexpr int kRuns = 8;
tick g_ran_at[kRuns];
int g_runs = 0;

metl::optional<tick> periodic(void*, tick now) noexcept {
  if (g_runs < kRuns) {
    g_ran_at[g_runs++] = now;
  }
  if (g_runs == kRuns) {
    return metl::nullopt;  // done
  }
  return now + kPeriod;
}

}  // namespace

int main() {
  int failures = 0;

  const tick start = g_ticks.now(&read_timer);
  g_tasks.schedule(nullptr, &periodic, start + kPeriod);

  // The main loop, one step per 1000 ticks of the fake timer. 8 runs of
  // 20000 ticks span 160000 ticks: the 16-bit counter wraps twice.
  tick previous = start;
  for (int step = 0; step < 200 && g_runs < kRuns; ++step) {
    g_counter = static_cast<std::uint16_t>(g_counter + 1000u);  // wraps on its own
    const tick now = g_ticks.now(&read_timer);
    if (now < previous) {
      std::printf("tick went backwards: %llu after %llu\n",
                  static_cast<unsigned long long>(now),
                  static_cast<unsigned long long>(previous));
      ++failures;
    }
    previous = now;
    (void)g_tasks.run_due(now);
  }

  if (g_runs != kRuns) {
    std::printf("expected %d runs, got %d\n", kRuns, g_runs);
    ++failures;
  }
  for (int i = 0; i < g_runs; ++i) {
    const tick expected = start + kPeriod * static_cast<tick>(i + 1);
    if (g_ran_at[i] != expected) {
      std::printf("run %d at %llu, expected %llu\n",
                  i,
                  static_cast<unsigned long long>(g_ran_at[i]),
                  static_cast<unsigned long long>(expected));
      ++failures;
    }
  }
  if (previous - start < 65536u * 2u) {
    std::printf("the counter did not wrap twice; the example proves nothing\n");
    ++failures;
  }

  std::printf("tick_extender: %d runs across %llu ticks of a 16-bit timer, %s\n",
              g_runs,
              static_cast<unsigned long long>(previous - start),
              failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
