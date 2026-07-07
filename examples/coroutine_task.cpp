// coroutine_task.cpp
//
// Cooperative multitasking with metl::coro::protothread — a stackless "thread"
// that yields control back to a super-loop scheduler without an RTOS, a real
// stack per task, dynamic allocation, or exceptions.
//
// A protothread is driven by repeatedly calling run(); each call advances the
// task until its next METL_PT_YIELD / METL_PT_WAIT_UNTIL and returns false
// (still running) or true (finished). Because it is stackless:
//
//   * State that must survive a yield lives in CLASS MEMBERS, never in locals.
//   * No two yield points may share a source line (the macros use __LINE__).
//
// This example runs two independent tasks off a single tick counter:
//   - blink_task  : toggles an "LED" every 3 ticks, for a fixed number of
//                   blinks, then finishes.
//   - ramp_task   : waits until a threshold tick, then counts up and exits.

#include <cstdint>
#include <cstdio>

#include <metl/coro/protothread.hpp>

namespace {

// A cooperative task that blinks an LED a fixed number of times.
class blink_task : public metl::coro::protothread {
 public:
  bool run() noexcept {
    METL_PT_BEGIN();
    for (blinks_ = 0; blinks_ < kMaxBlinks; ++blinks_) {
      // Wait 3 ticks with the LED on, then 3 ticks off. deadline_ is a member
      // so it survives the yields inside METL_PT_WAIT_UNTIL.
      led_ = true;
      ++transitions_;
      deadline_ = tick_ + 3;
      METL_PT_WAIT_UNTIL(tick_ >= deadline_);

      led_ = false;
      ++transitions_;
      deadline_ = tick_ + 3;
      METL_PT_WAIT_UNTIL(tick_ >= deadline_);
    }
    METL_PT_END();
  }

  void set_tick(std::uint32_t t) noexcept { tick_ = t; }
  bool led() const noexcept { return led_; }
  unsigned transitions() const noexcept { return transitions_; }

 private:
  static constexpr unsigned kMaxBlinks = 4;
  std::uint32_t tick_ = 0;
  std::uint32_t deadline_ = 0;
  unsigned blinks_ = 0;
  unsigned transitions_ = 0;
  bool led_ = false;
};

// A cooperative task that idles until tick 5, then counts three steps.
class ramp_task : public metl::coro::protothread {
 public:
  bool run() noexcept {
    METL_PT_BEGIN();
    METL_PT_WAIT_UNTIL(tick_ >= 5);
    for (count_ = 0; count_ < 3; ++count_) {
      value_ += 10;
      METL_PT_YIELD();
    }
    METL_PT_END();
  }

  void set_tick(std::uint32_t t) noexcept { tick_ = t; }
  int value() const noexcept { return value_; }

 private:
  std::uint32_t tick_ = 0;
  int count_ = 0;
  int value_ = 0;
};

}  // namespace

int main() {
  blink_task blink;
  ramp_task ramp;

  // The super-loop: advance a shared tick and poll every task until all done.
  std::uint32_t tick = 0;
  bool blink_done = false;
  bool ramp_done = false;
  for (; tick < 100 && (!blink_done || !ramp_done); ++tick) {
    blink.set_tick(tick);
    ramp.set_tick(tick);
    blink_done = blink.run();
    ramp_done = ramp.run();
  }

  // ---- Self-checks ----------------------------------------------------------
  if (!blink.is_done() || !ramp.is_done()) {
    return 1;
  }
  // 4 blinks -> 8 on/off transitions; LED ends off after the final cycle.
  if (blink.transitions() != 8 || blink.led()) {
    return 2;
  }
  // ramp waited until tick 5, then added 10 three times.
  if (ramp.value() != 30) {
    return 3;
  }

  std::printf("coroutine_task: blink transitions=%u ramp value=%d ticks=%u\n",
              blink.transitions(),
              ramp.value(),
              tick);
  return 0;
}
