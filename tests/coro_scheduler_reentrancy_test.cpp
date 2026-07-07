// Regression test: scheduler::run_once must stay memory-safe when a task
// detaches (or attaches) other tasks — or itself — from within its own poll().
//
// The previous implementation cached the vector size/index and compacted in
// place, so a detach() during poll() shifted elements and left run_once reading
// stale / out-of-bounds slots. Run under ASan/UBSan this test would trip on the
// old code.

#include "metl_check.hpp"

#include <cstddef>

#include <metl/coro/scheduler.hpp>

namespace {

using sched_t = metl::coro::scheduler<8>;

struct task {
  sched_t* sched = nullptr;
  int id = 0;
  int polls = 0;
  void* detach_target = nullptr;  // detach this task on first poll, then clear.
  bool detach_self = false;       // detach *this* on first poll.
  bool keep_running = true;       // otherwise report completion.
};

bool poll_task(void* p) noexcept {
  auto* t = static_cast<task*>(p);
  ++t->polls;
  if (t->detach_target != nullptr) {
    t->sched->detach(t->detach_target);
    t->detach_target = nullptr;
  }
  if (t->detach_self) {
    t->detach_self = false;
    t->sched->detach(t);
    return false;
  }
  return t->keep_running;
}

}  // namespace

int main() {
  // ---- Case 1: a task detaches a *later* task during its poll ----
  {
    sched_t sched;
    task a{&sched, 1, 0, nullptr, false, true};
    task b{&sched, 2, 0, nullptr, false, true};
    task c{&sched, 3, 0, nullptr, false, true};
    // 'a' detaches 'c' (which is attached after it) on its first poll.
    a.detach_target = &c;

    CHECK(sched.try_attach(&a, &poll_task));
    CHECK(sched.try_attach(&b, &poll_task));
    CHECK(sched.try_attach(&c, &poll_task));
    CHECK_EQ(sched.task_count(), std::size_t(3));

    const auto running = sched.run_once();
    // a and b polled once; c detached before it was reached => never polled.
    CHECK_EQ(a.polls, 1);
    CHECK_EQ(b.polls, 1);
    CHECK_EQ(c.polls, 0);
    CHECK_EQ(sched.task_count(), std::size_t(2));  // c gone
    CHECK_EQ(running, std::size_t(2));             // a and b still running
    CHECK(sched.is_attached(&a));
    CHECK(sched.is_attached(&b));
    CHECK(!sched.is_attached(&c));
  }

  // ---- Case 2: a task detaches an *earlier*, already-polled task ----
  {
    sched_t sched;
    task a{&sched, 1, 0, nullptr, false, true};
    task b{&sched, 2, 0, nullptr, false, true};
    b.detach_target = &a;  // b detaches a (already polled this round).

    CHECK(sched.try_attach(&a, &poll_task));
    CHECK(sched.try_attach(&b, &poll_task));

    const auto running = sched.run_once();
    CHECK_EQ(a.polls, 1);
    CHECK_EQ(b.polls, 1);
    CHECK_EQ(sched.task_count(), std::size_t(1));
    CHECK_EQ(running, std::size_t(2));
    CHECK(!sched.is_attached(&a));
    CHECK(sched.is_attached(&b));
  }

  // ---- Case 3: a task detaches *itself* during its poll ----
  {
    sched_t sched;
    task a{&sched, 1, 0, nullptr, true, true};
    task b{&sched, 2, 0, nullptr, false, true};

    CHECK(sched.try_attach(&a, &poll_task));
    CHECK(sched.try_attach(&b, &poll_task));

    sched.run_once();
    CHECK_EQ(a.polls, 1);
    CHECK_EQ(b.polls, 1);
    CHECK_EQ(sched.task_count(), std::size_t(1));
    CHECK(!sched.is_attached(&a));
    CHECK(sched.is_attached(&b));
  }

  // ---- Case 4: a task attaches a new task mid-round; new task not polled
  //             this round, but survives to the next ----
  {
    sched_t sched;
    static task late{};  // attached during a's poll.
    late = task{};
    late.sched = &sched;
    late.id = 99;

    struct attacher {
      sched_t* sched;
      task* late;
      int polls = 0;
    };
    static attacher at{&sched, &late, 0};
    at = attacher{&sched, &late, 0};

    auto attach_poll = +[](void* p) noexcept -> bool {
      auto* self = static_cast<attacher*>(p);
      ++self->polls;
      if (self->polls == 1) {
        (void)self->sched->try_attach(self->late, &poll_task);
      }
      return true;
    };

    CHECK(sched.try_attach(&at, attach_poll));
    const auto running = sched.run_once();
    CHECK_EQ(at.polls, 1);
    CHECK_EQ(late.polls, 0);                       // not polled the round it was added
    CHECK_EQ(running, std::size_t(1));             // only 'at' was polled
    CHECK_EQ(sched.task_count(), std::size_t(2));  // at + late
    // Next round polls both.
    sched.run_once();
    CHECK_EQ(late.polls, 1);
  }

  return metl_test::exit_code();
}
