// armed_timers.cpp
//
// Software timers that live in the objects using them, kept in two lists at
// once with metl::exp::intrusive_list: every timer, and the armed ones in
// deadline order.
//
// EXPERIMENTAL: metl::exp is outside the 1.x Semantic Versioning promise and
// may change in a minor release (docs/SCOPE.md section 3, Tier 2; #337).
//
// A timer derives from one hook per list it can be in:
//   * list_hook<all_timers>: in the list for as long as it exists, so a
//     diagnostic can walk every timer, armed or not;
//   * list_hook<armed>: in the list only while counting down, kept sorted by
//     deadline, so the next one due is always at the front.
//
// The case this shape serves is the timeout that almost never fires: a
// watchdog re-armed on every sign of life. Each re-arm cancels the pending
// deadline first, and cancelling is unlinking the timer, O(1) wherever it is in
// the list. Arming walks the list to keep it sorted, O(n) in the timers armed.
//
// What the hooks check, and coro::deadline_scheduler cannot (it holds a void*):
// a timer destroyed while armed, or armed twice, stops the program instead of
// leaving the list pointing at a dead object. Hence the declaration order in
// main(): the timers before the service, so the service's lists unlink them
// first.
//
// Modelled with a fake millisecond clock so the example is deterministic and
// self-checking; it returns non-zero if any dispatch is wrong.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <metl/exp/intrusive_list.hpp>

namespace {

using tick = std::uint32_t;

struct all_timers {};
struct armed {};

using every_hook = metl::exp::list_hook<all_timers>;
using armed_hook = metl::exp::list_hook<armed>;

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

struct timer : every_hook, armed_hook {
  timer(char timer_id, tick timer_period) noexcept : id(timer_id), period(timer_period) {}

  // With two hooks, `is_linked` names one of them.
  bool is_armed() const noexcept { return armed_hook::is_linked(); }

  char id;
  tick period;  // 0: one-shot
  tick deadline = 0;
  int fired = 0;
};

class timer_service {
 public:
  void add(timer& t) noexcept { every_.push_back(t); }

  // Arms `t` to expire at `at`, cancelling a pending deadline first. Equal
  // deadlines expire in the order they were armed.
  void arm(timer& t, tick at) noexcept {
    (void)cancel(t);
    t.deadline = at;
    auto pos = armed_.begin();
    while (pos != armed_.end() && pos->deadline <= at) {
      ++pos;
    }
    (void)armed_.insert(pos, t);
  }

  // Whether `t` was armed. O(1): the hook knows its neighbours.
  bool cancel(timer& t) noexcept {
    if (!t.is_armed()) {
      return false;
    }
    armed_.erase(t);
    return true;
  }

  // Expires every timer due at `now`; a periodic one is re-armed one period on.
  std::size_t run_due(tick now) noexcept {
    std::size_t ran = 0;
    while (!armed_.empty() && armed_.front().deadline <= now) {
      timer& t = armed_.front();
      armed_.pop_front();
      ++t.fired;
      record(t.id);
      if (t.period != 0) {
        arm(t, t.deadline + t.period);
      }
      ++ran;
    }
    return ran;
  }

  // The earliest deadline, or false when nothing is armed.
  bool next_deadline(tick& at) const noexcept {
    if (armed_.empty()) {
      return false;
    }
    at = armed_.front().deadline;
    return true;
  }

  std::size_t timer_count() const noexcept { return every_.size(); }
  std::size_t armed_count() const noexcept { return armed_.size(); }

  // Walks every timer, armed or not: what a diagnostic dump would do.
  template <typename F>
  void for_each_timer(F f) const {
    for (const timer& t : every_) {
      f(t);
    }
  }

 private:
  metl::exp::intrusive_list<timer, all_timers> every_;
  metl::exp::intrusive_list<timer, armed> armed_;
};

}  // namespace

int main() {
  // Declared before the service, so destroyed after it: the service's lists
  // unlink every timer when they are destroyed. The other way round, a timer
  // would be destroyed while linked, and its hook would stop the program.
  timer fast{'f', 10};
  timer slow{'s', 25};
  timer watchdog{'w', 0};  // one-shot: fires only if the kicks stop

  timer_service timers;
  timers.add(fast);
  timers.add(slow);
  timers.add(watchdog);

  constexpr tick kWatchdogTimeout = 15;
  constexpr tick kLastKick = 40;  // the sign of life stops after this tick

  timers.arm(fast, 10);
  timers.arm(slow, 25);
  timers.arm(watchdog, kWatchdogTimeout);

  // The idle loop an MCU would write: sleep until the next deadline, expire
  // what is due, and kick the watchdog on every fast tick until kLastKick.
  tick now = 0;
  std::size_t wakeups = 0;
  int cancelled_kicks = 0;
  while (now < 60) {
    tick next = 0;
    if (!timers.next_deadline(next)) {
      break;  // nothing armed
    }
    now = next;
    ++wakeups;
    if (timers.run_due(now) == 0) {
      return 1;  // we woke for a deadline, so something must have been due
    }
    if (now % 10 == 0 && now <= kLastKick) {
      cancelled_kicks += watchdog.is_armed() ? 1 : 0;
      timers.arm(watchdog, now + kWatchdogTimeout);  // cancels the pending deadline
    }
  }

  // Expected timeline, earliest deadline first, equal deadlines in arm order:
  //   10 f | 20 f | 25 s | 30 f | 40 f | 50 s,f | 55 w | 60 f
  // The watchdog, armed for 15, is pushed to 25, 35, 45 and 55 by the kicks at
  // 10..40, and fires at 55 once they stop. At 50 slow was re-armed (at 25)
  // before fast (at 40).
  static const char* const kExpected = "ffsffsfwf";
  for (std::size_t i = 0; kExpected[i] != '\0'; ++i) {
    if (g_log[i] != kExpected[i]) {
      std::printf(
          "dispatch order wrong at %zu: got '%c', want '%c' (log=%s)\n", i, g_log[i], kExpected[i], g_log);
      return 2;
    }
  }

  // Every kick found the watchdog armed and cancelled it; it fired only once.
  if (cancelled_kicks != 4) {
    std::printf("cancelled %d pending watchdog deadlines, expected 4\n", cancelled_kicks);
    return 3;
  }
  if (watchdog.fired != 1 || watchdog.is_armed()) {
    return 4;  // a one-shot fires once and is not re-armed
  }
  if (fast.fired != 6 || slow.fired != 2) {  // 10,20,30,40,50,60 and 25,50
    std::printf("fast ran %d times, slow %d; expected 6 and 2 (log=%s)\n", fast.fired, slow.fired, g_log);
    return 5;
  }
  if (wakeups != 8) {
    std::printf("woke %zu times, expected 8\n", wakeups);
    return 6;
  }

  // Disarming takes a timer out of one list only: it is still a timer.
  if (!timers.cancel(slow) || timers.cancel(slow)) {
    return 7;  // armed the first time, already disarmed the second
  }
  if (timers.armed_count() != 1 || timers.timer_count() != 3) {
    return 8;
  }
  int armed_seen = 0;
  timers.for_each_timer([&](const timer& t) {
    std::printf("  timer %c: fired %d, %s\n", t.id, t.fired, t.is_armed() ? "armed" : "idle");
    armed_seen += t.is_armed() ? 1 : 0;
  });
  if (armed_seen != 1) {
    return 9;
  }

  std::printf("armed_timers: %zu wakeups, dispatch log = %s, watchdog kicked %d times\n",
              wakeups,
              g_log,
              cancelled_kicks);
  return 0;
}
