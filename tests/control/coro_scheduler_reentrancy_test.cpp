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

// #260: identity is the (task, poll) pair, not the address. These re-attach the
// SAME object with a different poll function mid-round, so a scheduler that
// re-validates by address alone calls a poll function the task no longer has --
// with a different object at that address, a call through the wrong type.
// A task that may re-attach itself carries its scheduler, so no poll function
// reaches a test's local through a global.
struct counted {
  sched_t* sched = nullptr;
  int polls_a = 0;
  int polls_b = 0;
};
bool poll_a(void* p) noexcept {
  ++static_cast<counted*>(p)->polls_a;
  return true;
}
bool poll_b(void* p) noexcept {
  ++static_cast<counted*>(p)->polls_b;
  return true;
}

// A task whose poll re-attaches another task, `victim`, once.
struct swapper_t {
  sched_t* sched;
  counted* victim;
};

// Swaps the victim's poll function from poll_a to poll_b, once.
bool swap_victim_poll(void* p) noexcept {
  auto* s = static_cast<swapper_t*>(p);
  if (s->victim != nullptr) {
    CHECK(s->sched->detach(s->victim));
    CHECK(s->sched->try_attach(s->victim, &poll_b));
    s->victim = nullptr;
  }
  return true;
}

// Completes, but first re-attaches itself under poll_b: a task that restarts.
bool restart_as_b(void* p) noexcept {
  sched_t* sched = static_cast<counted*>(p)->sched;
  CHECK(sched->detach(p));
  CHECK(sched->try_attach(p, &poll_b));
  return false;
}

// Completes its first run, but first re-arms itself under the SAME poll
// function for another run (#286); completes for good on the second.
bool rearm_once(void* p) noexcept {
  auto* c = static_cast<counted*>(p);
  if (++c->polls_a == 1) {
    CHECK(c->sched->detach(p));
    CHECK(c->sched->try_attach(p, &rearm_once));
  }
  return false;
}

// Detaches the victim and attaches it again under the same poll_a, once: an
// attachment indistinguishable from the old one by (task, poll).
bool reattach_victim_same_poll(void* p) noexcept {
  auto* s = static_cast<swapper_t*>(p);
  if (s->victim != nullptr) {
    CHECK(s->sched->detach(s->victim));
    CHECK(s->sched->try_attach(s->victim, &poll_a));
    s->victim = nullptr;
  }
  return true;
}

// #310: attachment numbers are reused. A completing attachment must never
// remove a different one that shares its number, and a number taken mid-round
// must not revive a detached snapshot entry. The numbers cycle through
// 2 * Capacity + 1 values, so these loops reuse each of them many times.
using small_sched_t = metl::coro::scheduler<2>;

struct one_shot {
  int polls = 0;
};
bool poll_once(void* p) noexcept {
  ++static_cast<one_shot*>(p)->polls;
  return false;
}
bool poll_forever(void* p) noexcept {
  ++static_cast<one_shot*>(p)->polls;
  return true;
}

// Detaches `victim` (later in the round), attaches and detaches a filler
// `cycles` times to advance the attachment numbers, then leaves the filler
// attached. Across cycles = 0 .. 2 * Capacity, one of the runs offers the
// filler the victim's number.
struct churner {
  small_sched_t* sched;
  one_shot* victim;
  int cycles;
  one_shot filler;
};
bool churn(void* p) noexcept {
  auto* c = static_cast<churner*>(p);
  (void)c->sched->detach(c->victim);
  for (int i = 0; i < c->cycles; ++i) {
    if (c->sched->try_attach(&c->filler, &poll_forever)) {
      (void)c->sched->detach(&c->filler);
    }
  }
  (void)c->sched->try_attach(&c->filler, &poll_forever);
  return true;
}

// Runs a nested round from inside its own poll.
struct nester {
  small_sched_t* sched;
  int depth = 0;
};
bool nest(void* p) noexcept {
  auto* n = static_cast<nester*>(p);
  if (n->depth == 0) {
    ++n->depth;
    (void)n->sched->run_once();
  }
  return false;
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
    // Static like `late` and `at` below: they hold its address, so the
    // scheduler must not outlive them the other way round.
    static sched_t sched;
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

  // ---- #260: a mid-round re-attach under another poll function --------------
  {
    sched_t sched;
    counted victim;
    swapper_t swapper{&sched, &victim};
    CHECK(sched.try_attach(&swapper, &swap_victim_poll));
    CHECK(sched.try_attach(&victim, &poll_a));
    (void)sched.run_once();
    // The snapshot's poll_a must not run on a task now attached under poll_b,
    // and an attachment made mid-round waits for the next round.
    CHECK_EQ(victim.polls_a, 0);
    CHECK_EQ(victim.polls_b, 0);
    (void)sched.run_once();
    CHECK_EQ(victim.polls_a, 0);
    CHECK_EQ(victim.polls_b, 1);
  }
  {
    sched_t sched;
    counted task_b;
    task_b.sched = &sched;
    CHECK(sched.try_attach(&task_b, &restart_as_b));
    (void)sched.run_once();
    // Completion removes the attachment that completed, not the one the task
    // made of itself during that poll.
    CHECK(sched.is_attached(&task_b));
    CHECK_EQ(sched.task_count(), std::size_t{1});
    (void)sched.run_once();
    CHECK_EQ(task_b.polls_b, 1);
  }

  // ---- #286: identity is the attachment, not the (task, poll) pair -----------
  {
    sched_t sched;
    counted job;
    job.sched = &sched;
    CHECK(sched.try_attach(&job, &rearm_once));
    (void)sched.run_once();
    // The run that completed is removed; the re-arm it made of itself is not.
    CHECK(sched.is_attached(&job));
    CHECK_EQ(job.polls_a, 1);
    (void)sched.run_once();
    CHECK_EQ(job.polls_a, 2);
    CHECK(!sched.is_attached(&job));
  }
  {
    sched_t sched;
    counted victim;
    swapper_t swapper{&sched, &victim};
    CHECK(sched.try_attach(&swapper, &reattach_victim_same_poll));
    CHECK(sched.try_attach(&victim, &poll_a));
    (void)sched.run_once();
    // Re-attached mid-round with the same poll function: still a new
    // attachment, so it waits for the next round like any other.
    CHECK_EQ(victim.polls_a, 0);
    (void)sched.run_once();
    CHECK_EQ(victim.polls_a, 1);
  }

  // ---- #310: reused attachment numbers ---------------------------------------
  {
    small_sched_t sched;
    one_shot steady;
    one_shot burst;
    CHECK(sched.try_attach(&steady, &poll_forever));
    for (int i = 0; i < 100; ++i) {
      CHECK(sched.try_attach(&burst, &poll_once));
      (void)sched.run_once();
      // Only the attachment that completed goes, whatever number it reused.
      CHECK(sched.is_attached(&steady));
      CHECK(!sched.is_attached(&burst));
    }
    CHECK_EQ(steady.polls, 100);
    CHECK_EQ(burst.polls, 100);
  }
  for (int cycles = 0; cycles <= 2 * 2; ++cycles) {
    small_sched_t sched;
    one_shot victim;
    churner c{&sched, &victim, cycles, {}};
    CHECK(sched.try_attach(&c, &churn));
    CHECK(sched.try_attach(&victim, &poll_forever));
    (void)sched.run_once();
    // Detached earlier in the round: its snapshot entry is not polled, and the
    // filler attached mid-round waits for the next round, whichever number it
    // was given.
    CHECK_EQ(victim.polls, 0);
    CHECK_EQ(c.filler.polls, 0);
    CHECK(!sched.is_attached(&victim));
    CHECK(sched.is_attached(&c.filler));
  }
  {
    small_sched_t sched;
    nester outer{&sched};
    one_shot other;
    CHECK(sched.try_attach(&outer, &nest));
    CHECK(sched.try_attach(&other, &poll_forever));
    (void)sched.run_once();
    // The nested round polled `other` once and the outer round once more.
    CHECK_EQ(other.polls, 2);
    CHECK(!sched.is_attached(&outer));
    CHECK(sched.is_attached(&other));
  }

  return metl_test::exit_code();
}
