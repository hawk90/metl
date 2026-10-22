// metl::coro::scheduler: attaching protothreads and steppers, detach,
// run_until_idle's bound and hand-offs (the contract, shared with the
// intrusive variant in scheduler_contract.hpp), and capacity exhaustion.

#include "control/scheduler_contract.hpp"

#include <metl/coro/scheduler.hpp>

namespace {

using metl_test::array_policy;
using metl_test::scheduler_contract::n_yield_thread;

}  // namespace

int main() {
  if (const int failed = metl_test::scheduler_contract::basic_contract<array_policy>()) {
    return failed;
  }

  // ---- Capacity exhaustion: try_attach returns false when full. --------
  {
    n_yield_thread<array_policy> x;
    x.target = 1;
    n_yield_thread<array_policy> y;
    y.target = 1;
    n_yield_thread<array_policy> z;
    z.target = 1;
    metl::coro::scheduler<2> sched;
    if (sched.capacity() != 2) {
      return 48;
    }

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

  return 0;
}
