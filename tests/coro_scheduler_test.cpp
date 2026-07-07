#include <metl/coro/protothread.hpp>
#include <metl/coro/scheduler.hpp>
#include <metl/coro/stepper.hpp>

namespace {

// Protothread that yields N times then finishes.
class n_yield_thread : public metl::coro::protothread {
 public:
  int target = 0;
  int hits = 0;

  bool run() noexcept {
    METL_PT_BEGIN();
    while (hits < target) {
      ++hits;
      METL_PT_YIELD();
    }
    METL_PT_END();
  }
};

// Stepper that yields N times then completes.
class n_step_task : public metl::coro::stepper {
 public:
  int target = 0;
  int hits = 0;

  metl::coro::step_result step() noexcept override {
    if (hits < target) {
      ++hits;
      return metl::coro::step_result::yield;
    }
    return metl::coro::step_result::done;
  }
};

}  // namespace

int main() {
  // ---- Attach 3 protothreads + 2 steppers, drive to completion. --------
  {
    metl::coro::scheduler<8> sched;

    n_yield_thread t1;
    t1.target = 2;
    n_yield_thread t2;
    t2.target = 3;
    n_yield_thread t3;
    t3.target = 1;
    n_step_task s1;
    s1.target = 2;
    n_step_task s2;
    s2.target = 4;

    if (!sched.try_attach_protothread(t1)) {
      return 1;
    }
    if (!sched.try_attach_protothread(t2)) {
      return 2;
    }
    if (!sched.try_attach_protothread(t3)) {
      return 3;
    }
    if (!sched.try_attach_stepper(s1)) {
      return 4;
    }
    if (!sched.try_attach_stepper(s2)) {
      return 5;
    }

    if (sched.task_count() != 5) {
      return 6;
    }
    if (sched.empty()) {
      return 7;
    }
    if (sched.capacity() != 8) {
      return 8;
    }

    // Round 1: every task runs once and yields. None complete.
    {
      const auto running = sched.run_once();
      // run_once polls protothreads: each ran once and is still incomplete.
      // Steppers also yield. So all five should remain.
      if (running != 5) {
        return 10;
      }
      if (sched.task_count() != 5) {
        return 11;
      }
    }

    // Round 2: t3 should finish (target=1, ran once already, second run exits).
    {
      const auto running = sched.run_once();
      // After round 2: t1 hits=2 still in while, t2 hits=2, t3 finished,
      // s1 hits=2 next call done, s2 hits=2 still going.
      // Protothreads run() returns true only when the while-loop exits.
      // So t3 (target=1, hits=1 after round 1) on its second run sees hits==target,
      // skips the while body, hits METL_PT_END -> returns true -> removed.
      if (running != 4) {
        return 12;
      }
      if (sched.task_count() != 4) {
        return 13;
      }
    }

    // Drive everything else to completion.
    const auto rounds = sched.run_until_idle();
    if (rounds == 0) {
      return 14;
    }
    if (!sched.empty()) {
      return 15;
    }
    if (sched.task_count() != 0) {
      return 16;
    }
    // Verify counts.
    if (t1.hits != 2) {
      return 17;
    }
    if (t2.hits != 3) {
      return 18;
    }
    if (t3.hits != 1) {
      return 19;
    }
    if (s1.hits != 2) {
      return 20;
    }
    if (s2.hits != 4) {
      return 21;
    }
  }

  // ---- detach() removes the task without polling it. --------------------
  {
    metl::coro::scheduler<4> sched;
    n_yield_thread a;
    a.target = 10;
    n_yield_thread b;
    b.target = 10;
    if (!sched.try_attach_protothread(a)) {
      return 30;
    }
    if (!sched.try_attach_protothread(b)) {
      return 31;
    }

    if (!sched.detach(&a)) {
      return 32;
    }
    if (sched.task_count() != 1) {
      return 33;
    }

    // detach of an already-removed task returns false.
    if (sched.detach(&a)) {
      return 34;
    }

    // Run once, only `b` should advance.
    const auto running = sched.run_once();
    if (running != 1) {
      return 35;
    }
    if (a.hits != 0) {
      return 36;
    }
    if (b.hits != 1) {
      return 37;
    }
  }

  // ---- Capacity exhaustion: try_attach returns false when full. --------
  {
    metl::coro::scheduler<2> sched;
    n_yield_thread x;
    x.target = 1;
    n_yield_thread y;
    y.target = 1;
    n_yield_thread z;
    z.target = 1;

    if (!sched.try_attach_protothread(x)) {
      return 40;
    }
    if (!sched.try_attach_protothread(y)) {
      return 41;
    }
    // Full now.
    if (sched.try_attach_protothread(z)) {
      return 42;
    }
    if (sched.task_count() != 2) {
      return 43;
    }

    // Drain so a slot opens.
    const auto rounds = sched.run_until_idle();
    (void)rounds;
    if (!sched.empty()) {
      return 44;
    }

    // Attach z now succeeds.
    if (!sched.try_attach_protothread(z)) {
      return 45;
    }
    sched.run_until_idle();
    if (!sched.empty()) {
      return 46;
    }
    if (z.hits != 1) {
      return 47;
    }
  }

  // ---- run_until_idle respects max_rounds. ------------------------------
  {
    metl::coro::scheduler<2> sched;
    // Task that never finishes during the round budget.
    n_yield_thread forever;
    forever.target = 100000;
    if (!sched.try_attach_protothread(forever)) {
      return 50;
    }
    const auto rounds = sched.run_until_idle(7);
    if (rounds != 7) {
      return 51;
    }
    if (sched.empty()) {
      return 52;
    }
    if (forever.hits != 7) {
      return 53;
    }
  }

  return 0;
}
