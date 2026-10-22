// The intrusive scheduler variant (tests/exp/intrusive_scheduler.hpp, #337)
// against the scheduler contract metl::coro::scheduler is tested with
// (tests/control/scheduler_contract.hpp), and the checks only its hook can
// make.

#include "exp/intrusive_scheduler.hpp"

#include "assert_trip.hpp"
#include "control/scheduler_contract.hpp"
#include "metl_check.hpp"

#include <cstddef>
#include <new>

#include <metl/exp/intrusive_list.hpp>

namespace {

using metl_test::intrusive_scheduler;
using metl_test::task_node;
using metl_test::trips;

struct intrusive_policy {
  template <std::size_t>
  using sched = intrusive_scheduler;  // no capacity
  using node = task_node;

  template <typename T>
  static bool attach(intrusive_scheduler& s, T& task, metl::coro::poll_fn fn) noexcept {
    s.attach(task, &task, fn);
    return true;  // never full
  }
  template <typename T>
  static bool attach_protothread(intrusive_scheduler& s, T& task) noexcept {
    s.attach_protothread(task);
    return true;
  }
  template <typename T>
  static bool attach_stepper(intrusive_scheduler& s, T& task) noexcept {
    s.attach_stepper(task);
    return true;
  }
  template <typename T>
  static bool detach(intrusive_scheduler& s, T& task) noexcept {
    return s.detach(task);
  }
  template <typename T>
  static bool is_attached(const intrusive_scheduler& s, T& task) noexcept {
    return s.is_attached(task);
  }
};

struct job : task_node {
  int polls = 0;
};

bool poll_job(void* p) noexcept {
  ++static_cast<job*>(p)->polls;
  return true;
}

bool poll_job_otherwise(void* p) noexcept {
  static_cast<job*>(p)->polls += 100;
  return true;
}

// Where scheduler<Capacity> would poll a destroyed task later, the hook stops
// the program when the task is destroyed.
void destroying_an_attached_task_traps() {
  alignas(job) unsigned char storage[sizeof(job)];
  intrusive_scheduler sched;
  job* t = ::new (static_cast<void*>(storage)) job;
  sched.attach(*t, t, &poll_job);
  CHECK(trips([&] { t->~job(); }));
  // The trap left before the hook's destructor finished; detaching resets
  // the link fields in its storage.
  CHECK(sched.detach(*t));
  CHECK(sched.empty());
}

// Attached twice, to the same scheduler or another, before any slot is
// written: the live attachment keeps its poll function.
void attaching_an_attached_task_traps() {
  job t;
  intrusive_scheduler first;
  intrusive_scheduler second;
  first.attach(t, &t, &poll_job);
  CHECK(trips([&] { first.attach(t, &t, &poll_job_otherwise); }));
  CHECK(trips([&] { second.attach(t, &t, &poll_job_otherwise); }));
  CHECK_EQ(first.task_count(), std::size_t{1});
  CHECK(second.empty());
  CHECK_EQ(first.run_once(), std::size_t{1});
  CHECK_EQ(t.polls, 1);
  CHECK(first.detach(t));
}

// A null poll function would read as a round's marker and never run.
void a_null_poll_traps() {
  job t;
  intrusive_scheduler sched;
  CHECK(trips([&] { sched.attach(t, &t, nullptr); }));
  CHECK(!sched.is_attached(t));
}

}  // namespace

int main() {
  namespace contract = metl_test::scheduler_contract;
  CHECK_EQ(contract::basic_contract<intrusive_policy>(), 0);
  contract::reentrancy_contract<intrusive_policy>();

  destroying_an_attached_task_traps();
  attaching_an_attached_task_traps();
  a_null_poll_traps();

  return metl_test::exit_code();
}
