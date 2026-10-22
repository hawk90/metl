// Regression test: scheduler::run_once must stay memory-safe when a task
// detaches (or attaches) other tasks — or itself — from within its own poll().
//
// The previous implementation cached the vector size/index and compacted in
// place, so a detach() during poll() shifted elements and left run_once reading
// stale / out-of-bounds slots. Run under ASan/UBSan this test would trip on the
// old code.
//
// The contract cases live in scheduler_contract.hpp, shared with the intrusive
// variant (#337); the case here is the array's alone.

#include "control/scheduler_contract.hpp"
#include "metl_check.hpp"

#include <cstddef>

#include <metl/coro/scheduler.hpp>

namespace {

// ---- Array only: nested rounds hold every attachment number ----
// Three rounds nested inside polls, each holding one attachment number, use
// all 2 * Capacity + 1 of them at Capacity 1. Each level detaches itself,
// attaches the next level and runs a round, which polls it; the innermost,
// with no task attached, then tries one more attach. The intrusive variant
// numbers nothing, so it has no such limit.
using tiny_sched_t = metl::coro::scheduler<1>;

struct level {
  tiny_sched_t* sched = nullptr;
  level* next = nullptr;
  void* probe = nullptr;
  bool probe_attached = true;  // what the innermost try_attach returned
};

bool poll_forever_tiny(void*) noexcept {
  return true;
}

bool descend(void* p) noexcept {
  auto* l = static_cast<level*>(p);
  (void)l->sched->detach(l);
  if (l->next != nullptr) {
    CHECK(l->sched->try_attach(l->next, &descend));
    (void)l->sched->run_once();
  } else {
    CHECK_EQ(l->sched->task_count(), std::size_t{0});
    l->probe_attached = l->sched->try_attach(l->probe, &poll_forever_tiny);
  }
  return false;
}

}  // namespace

int main() {
  metl_test::scheduler_contract::reentrancy_contract<metl_test::array_policy>();

  // try_attach reports false although no task is attached, as its contract
  // says; once the rounds unwind, the numbers are free again.
  {
    tiny_sched_t sched;
    int probe = 0;
    level innermost{&sched, nullptr, &probe};
    level middle{&sched, &innermost, nullptr};
    level outer{&sched, &middle, nullptr};
    CHECK(sched.try_attach(&outer, &descend));
    (void)sched.run_once();
    CHECK(!innermost.probe_attached);
    CHECK_EQ(sched.task_count(), std::size_t{0});
    CHECK(sched.try_attach(&probe, &poll_forever_tiny));
  }

  return metl_test::exit_code();
}
