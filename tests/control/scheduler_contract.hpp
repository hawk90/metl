#pragma once

// The scheduler contract, as templates over the scheduler under test, so one
// body checks metl::coro::scheduler (tests/control/coro_scheduler*_test.cpp)
// and the intrusive variant it is measured against (#337;
// tests/exp/intrusive_scheduler_test.cpp).
//
// A policy names the scheduler type and how a task attaches, detaches and is
// looked up; a task derives from `Policy::node`, which is empty for the array
// version. Every task is declared before its scheduler: the intrusive
// variant's hook stops the program when a task still attached is destroyed,
// and a scheduler destroyed first unlinks every task.
//
// This header includes no metl/exp/ header, so the tests of the stable
// scheduler still build against the single-header amalgamation.

#include "metl_check.hpp"

#include <cstddef>
#include <cstring>
#include <new>

#include <metl/coro/protothread.hpp>
#include <metl/coro/scheduler.hpp>
#include <metl/coro/stepper.hpp>

namespace metl_test {

struct array_policy {
  template <std::size_t Capacity>
  using sched = metl::coro::scheduler<Capacity>;
  struct node {};  // the array stores the attachment itself

  template <typename S, typename T>
  static bool attach(S& s, T& task, metl::coro::poll_fn fn) noexcept {
    return s.try_attach(&task, fn);
  }
  template <typename S, typename T>
  static bool attach_protothread(S& s, T& task) noexcept {
    return s.try_attach_protothread(task);
  }
  template <typename S, typename T>
  static bool attach_stepper(S& s, T& task) noexcept {
    return s.try_attach_stepper(task);
  }
  template <typename S, typename T>
  static bool detach(S& s, T& task) noexcept {
    return s.detach(&task);
  }
  template <typename S, typename T>
  static bool is_attached(const S& s, T& task) noexcept {
    return s.is_attached(&task);
  }
};

namespace scheduler_contract {

// Protothread that yields N times then finishes.
template <typename P>
class n_yield_thread : public metl::coro::protothread, public P::node {
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
template <typename P>
class n_step_task : public metl::coro::stepper, public P::node {
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

// A task that, in its final poll, attaches the next step and completes: a
// pipeline hand-off. run_once polls the successor in the next round, not this
// one, so that round reports nothing yielded although a task is attached.
template <typename P>
struct hand_off : P::node {
  typename P::template sched<4>* sched = nullptr;
  hand_off* next = nullptr;
  int polls = 0;
};

template <typename P>
bool poll_hand_off(void* p) noexcept {
  auto* step = static_cast<hand_off<P>*>(p);
  ++step->polls;
  if (step->next != nullptr) {
    (void)P::attach(*step->sched, *step->next, &poll_hand_off<P>);
  }
  return false;
}

template <typename P>
int basic_contract() {
  // ---- Attach 3 protothreads + 2 steppers, drive to completion. --------
  {
    n_yield_thread<P> t1;
    t1.target = 2;
    n_yield_thread<P> t2;
    t2.target = 3;
    n_yield_thread<P> t3;
    t3.target = 1;
    n_step_task<P> s1;
    s1.target = 2;
    n_step_task<P> s2;
    s2.target = 4;
    typename P::template sched<8> sched;

    if (!P::attach_protothread(sched, t1)) {
      return 1;
    }
    if (!P::attach_protothread(sched, t2)) {
      return 2;
    }
    if (!P::attach_protothread(sched, t3)) {
      return 3;
    }
    if (!P::attach_stepper(sched, s1)) {
      return 4;
    }
    if (!P::attach_stepper(sched, s2)) {
      return 5;
    }

    if (sched.task_count() != 5) {
      return 6;
    }
    if (sched.empty()) {
      return 7;
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
    n_yield_thread<P> a;
    a.target = 10;
    n_yield_thread<P> b;
    b.target = 10;
    typename P::template sched<4> sched;
    if (!P::attach_protothread(sched, a)) {
      return 30;
    }
    if (!P::attach_protothread(sched, b)) {
      return 31;
    }

    if (!P::detach(sched, a)) {
      return 32;
    }
    if (sched.task_count() != 1) {
      return 33;
    }

    // detach of an already-removed task returns false.
    if (P::detach(sched, a)) {
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

  // ---- run_until_idle respects max_rounds. ------------------------------
  {
    // Task that never finishes during the round budget.
    n_yield_thread<P> forever;
    forever.target = 100000;
    typename P::template sched<2> sched;
    if (!P::attach_protothread(sched, forever)) {
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

  // ---- run_until_idle runs tasks attached in a round that yielded nothing.
  // It used to stop after the first hand-off: that round yielded nothing, so
  // it returned with the next step attached and never polled.
  {
    hand_off<P> c;
    hand_off<P> b;
    hand_off<P> a;
    typename P::template sched<4> sched;
    c.sched = &sched;
    b.sched = &sched;
    b.next = &c;
    a.sched = &sched;
    a.next = &b;
    if (!P::attach(sched, a, &poll_hand_off<P>)) {
      return 60;
    }
    const auto rounds = sched.run_until_idle();
    if (rounds != 3 || !sched.empty()) {
      return 61;
    }
    if (a.polls != 1 || b.polls != 1 || c.polls != 1) {
      return 62;
    }
  }

  // ---- ... and is still bounded by max_rounds for a hand-off that never ends.
  {
    hand_off<P> a;
    hand_off<P> b;
    typename P::template sched<4> sched;
    a.sched = &sched;
    b.sched = &sched;
    b.next = &a;
    a.next = &b;
    if (!P::attach(sched, a, &poll_hand_off<P>)) {
      return 63;
    }
    const auto rounds = sched.run_until_idle(9);
    if (rounds != 9 || sched.task_count() != 1) {
      return 64;
    }
    if (a.polls + b.polls != 9) {
      return 65;
    }
  }

  return 0;
}

template <typename P>
using sched_t = typename P::template sched<8>;

template <typename P>
struct task : P::node {
  sched_t<P>* sched = nullptr;
  int id = 0;
  int polls = 0;
  task* detach_target = nullptr;  // detach this task on first poll, then clear.
  bool detach_self = false;       // detach *this* on first poll.
  bool keep_running = true;       // otherwise report completion.
};

template <typename P>
bool poll_task(void* p) noexcept {
  auto* t = static_cast<task<P>*>(p);
  ++t->polls;
  if (t->detach_target != nullptr) {
    (void)P::detach(*t->sched, *t->detach_target);
    t->detach_target = nullptr;
  }
  if (t->detach_self) {
    t->detach_self = false;
    (void)P::detach(*t->sched, *t);
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
template <typename P>
struct counted : P::node {
  sched_t<P>* sched = nullptr;
  int polls_a = 0;
  int polls_b = 0;
};
template <typename P>
bool poll_a(void* p) noexcept {
  ++static_cast<counted<P>*>(p)->polls_a;
  return true;
}
template <typename P>
bool poll_b(void* p) noexcept {
  ++static_cast<counted<P>*>(p)->polls_b;
  return true;
}

// A task whose poll re-attaches another task, `victim`, once.
template <typename P>
struct swapper_t : P::node {
  sched_t<P>* sched = nullptr;
  counted<P>* victim = nullptr;
};

// Swaps the victim's poll function from poll_a to poll_b, once.
template <typename P>
bool swap_victim_poll(void* p) noexcept {
  auto* s = static_cast<swapper_t<P>*>(p);
  if (s->victim != nullptr) {
    CHECK(P::detach(*s->sched, *s->victim));
    CHECK(P::attach(*s->sched, *s->victim, &poll_b<P>));
    s->victim = nullptr;
  }
  return true;
}

// Completes, but first re-attaches itself under poll_b: a task that restarts.
template <typename P>
bool restart_as_b(void* p) noexcept {
  auto* c = static_cast<counted<P>*>(p);
  CHECK(P::detach(*c->sched, *c));
  CHECK(P::attach(*c->sched, *c, &poll_b<P>));
  return false;
}

// Completes its first run, but first re-arms itself under the SAME poll
// function for another run (#286); completes for good on the second.
template <typename P>
bool rearm_once(void* p) noexcept {
  auto* c = static_cast<counted<P>*>(p);
  if (++c->polls_a == 1) {
    CHECK(P::detach(*c->sched, *c));
    CHECK(P::attach(*c->sched, *c, &rearm_once<P>));
  }
  return false;
}

// Detaches the victim and attaches it again under the same poll_a, once: an
// attachment indistinguishable from the old one by (task, poll).
template <typename P>
bool reattach_victim_same_poll(void* p) noexcept {
  auto* s = static_cast<swapper_t<P>*>(p);
  if (s->victim != nullptr) {
    CHECK(P::detach(*s->sched, *s->victim));
    CHECK(P::attach(*s->sched, *s->victim, &poll_a<P>));
    s->victim = nullptr;
  }
  return true;
}

// #310: attachment numbers are reused. A completing attachment must never
// remove a different one that shares its number, and a number taken mid-round
// must not revive a detached snapshot entry. The numbers cycle through
// 2 * Capacity + 1 values, so these loops reuse each of them many times.
template <typename P>
using small_sched_t = typename P::template sched<2>;

template <typename P>
struct one_shot : P::node {
  int polls = 0;
};
template <typename P>
bool poll_once(void* p) noexcept {
  ++static_cast<one_shot<P>*>(p)->polls;
  return false;
}
template <typename P>
bool poll_forever(void* p) noexcept {
  ++static_cast<one_shot<P>*>(p)->polls;
  return true;
}

// Detaches `victim` (later in the round), attaches and detaches a filler
// `cycles` times to advance the attachment numbers, then leaves the filler
// attached. Across cycles = 0 .. 2 * Capacity, one of the runs offers the
// filler the victim's number.
template <typename P>
struct churner : P::node {
  small_sched_t<P>* sched = nullptr;
  one_shot<P>* victim = nullptr;
  int cycles = 0;
  one_shot<P> filler;
};
template <typename P>
bool churn(void* p) noexcept {
  auto* c = static_cast<churner<P>*>(p);
  (void)P::detach(*c->sched, *c->victim);
  for (int i = 0; i < c->cycles; ++i) {
    if (P::attach(*c->sched, c->filler, &poll_forever<P>)) {
      (void)P::detach(*c->sched, c->filler);
    }
  }
  (void)P::attach(*c->sched, c->filler, &poll_forever<P>);
  return true;
}

// Runs a nested round from inside its own poll.
template <typename P>
struct nester : P::node {
  small_sched_t<P>* sched = nullptr;
  int depth = 0;
  std::size_t count_in_round = 0;  // task_count() seen from inside the round
};
template <typename P>
bool nest(void* p) noexcept {
  auto* n = static_cast<nester<P>*>(p);
  if (n->depth == 0) {
    ++n->depth;
    n->count_in_round = n->sched->task_count();
    (void)n->sched->run_once();
  }
  return false;
}

// On its first poll, attaches `late`.
template <typename P>
struct attacher : P::node {
  sched_t<P>* sched = nullptr;
  task<P>* late = nullptr;
  int polls = 0;
};
template <typename P>
bool attach_late(void* p) noexcept {
  auto* self = static_cast<attacher<P>*>(p);
  ++self->polls;
  if (self->polls == 1) {
    (void)P::attach(*self->sched, *self->late, &poll_task<P>);
  }
  return true;
}

// Detaches itself, ends its own lifetime and lets its storage be reused, all
// inside its poll, then reports completion. The scheduler must not read the
// task after that poll: the bytes are no longer a task.
template <typename P>
struct self_destroying : P::node {
  sched_t<P>* sched = nullptr;
  unsigned char* storage = nullptr;
};
template <typename P>
bool destroy_self(void* p) noexcept {
  auto* t = static_cast<self_destroying<P>*>(p);
  unsigned char* const storage = t->storage;
  (void)P::detach(*t->sched, *t);
  t->~self_destroying();
  std::memset(storage, 0xA5, sizeof(self_destroying<P>));
  return false;
}

template <typename P>
void reentrancy_contract() {
  // ---- Case 1: a task detaches a *later* task during its poll ----
  {
    task<P> a;
    task<P> b;
    task<P> c;
    sched_t<P> sched;
    a.sched = &sched;
    b.sched = &sched;
    c.sched = &sched;
    // 'a' detaches 'c' (which is attached after it) on its first poll.
    a.detach_target = &c;

    CHECK(P::attach(sched, a, &poll_task<P>));
    CHECK(P::attach(sched, b, &poll_task<P>));
    CHECK(P::attach(sched, c, &poll_task<P>));
    CHECK_EQ(sched.task_count(), std::size_t(3));

    const auto running = sched.run_once();
    // a and b polled once; c detached before it was reached => never polled.
    CHECK_EQ(a.polls, 1);
    CHECK_EQ(b.polls, 1);
    CHECK_EQ(c.polls, 0);
    CHECK_EQ(sched.task_count(), std::size_t(2));  // c gone
    CHECK_EQ(running, std::size_t(2));             // a and b still running
    CHECK(P::is_attached(sched, a));
    CHECK(P::is_attached(sched, b));
    CHECK(!P::is_attached(sched, c));
  }

  // ---- Case 2: a task detaches an *earlier*, already-polled task ----
  {
    task<P> a;
    task<P> b;
    sched_t<P> sched;
    a.sched = &sched;
    b.sched = &sched;
    b.detach_target = &a;  // b detaches a (already polled this round).

    CHECK(P::attach(sched, a, &poll_task<P>));
    CHECK(P::attach(sched, b, &poll_task<P>));

    const auto running = sched.run_once();
    CHECK_EQ(a.polls, 1);
    CHECK_EQ(b.polls, 1);
    CHECK_EQ(sched.task_count(), std::size_t(1));
    CHECK_EQ(running, std::size_t(2));
    CHECK(!P::is_attached(sched, a));
    CHECK(P::is_attached(sched, b));
  }

  // ---- Case 3: a task detaches *itself* during its poll ----
  {
    task<P> a;
    task<P> b;
    sched_t<P> sched;
    a.sched = &sched;
    a.detach_self = true;
    b.sched = &sched;

    CHECK(P::attach(sched, a, &poll_task<P>));
    CHECK(P::attach(sched, b, &poll_task<P>));

    sched.run_once();
    CHECK_EQ(a.polls, 1);
    CHECK_EQ(b.polls, 1);
    CHECK_EQ(sched.task_count(), std::size_t(1));
    CHECK(!P::is_attached(sched, a));
    CHECK(P::is_attached(sched, b));
  }

  // ---- Case 4: a task attaches a new task mid-round; new task not polled
  //             this round, but survives to the next ----
  {
    task<P> late;  // attached during at's poll.
    attacher<P> at;
    sched_t<P> sched;
    late.sched = &sched;
    late.id = 99;
    at.sched = &sched;
    at.late = &late;

    CHECK(P::attach(sched, at, &attach_late<P>));
    const auto running = sched.run_once();
    CHECK_EQ(at.polls, 1);
    CHECK_EQ(late.polls, 0);                       // not polled the round it was added
    CHECK_EQ(running, std::size_t(1));             // only 'at' was polled
    CHECK_EQ(sched.task_count(), std::size_t(2));  // at + late
    // Next round polls both.
    sched.run_once();
    CHECK_EQ(late.polls, 1);
  }

  // ---- A task destroyed in its own poll, its storage reused ----
  {
    task<P> after;
    alignas(self_destroying<P>) unsigned char storage[sizeof(self_destroying<P>)];
    sched_t<P> sched;
    auto* doomed = new (storage) self_destroying<P>;
    doomed->sched = &sched;
    doomed->storage = storage;
    CHECK(P::attach(sched, *doomed, &destroy_self<P>));
    CHECK(P::attach(sched, after, &poll_task<P>));
    CHECK_EQ(sched.run_once(), std::size_t{1});
    CHECK_EQ(after.polls, 1);
    CHECK_EQ(sched.task_count(), std::size_t{1});
  }

  // ---- #260: a mid-round re-attach under another poll function --------------
  {
    counted<P> victim;
    swapper_t<P> swapper;
    sched_t<P> sched;
    swapper.sched = &sched;
    swapper.victim = &victim;
    CHECK(P::attach(sched, swapper, &swap_victim_poll<P>));
    CHECK(P::attach(sched, victim, &poll_a<P>));
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
    counted<P> task_b;
    sched_t<P> sched;
    task_b.sched = &sched;
    CHECK(P::attach(sched, task_b, &restart_as_b<P>));
    (void)sched.run_once();
    // Completion removes the attachment that completed, not the one the task
    // made of itself during that poll.
    CHECK(P::is_attached(sched, task_b));
    CHECK_EQ(sched.task_count(), std::size_t{1});
    (void)sched.run_once();
    CHECK_EQ(task_b.polls_b, 1);
  }

  // ---- #286: identity is the attachment, not the (task, poll) pair -----------
  {
    counted<P> job;
    sched_t<P> sched;
    job.sched = &sched;
    CHECK(P::attach(sched, job, &rearm_once<P>));
    (void)sched.run_once();
    // The run that completed is removed; the re-arm it made of itself is not.
    CHECK(P::is_attached(sched, job));
    CHECK_EQ(job.polls_a, 1);
    (void)sched.run_once();
    CHECK_EQ(job.polls_a, 2);
    CHECK(!P::is_attached(sched, job));
  }
  {
    counted<P> victim;
    swapper_t<P> swapper;
    sched_t<P> sched;
    swapper.sched = &sched;
    swapper.victim = &victim;
    CHECK(P::attach(sched, swapper, &reattach_victim_same_poll<P>));
    CHECK(P::attach(sched, victim, &poll_a<P>));
    (void)sched.run_once();
    // Re-attached mid-round with the same poll function: still a new
    // attachment, so it waits for the next round like any other.
    CHECK_EQ(victim.polls_a, 0);
    (void)sched.run_once();
    CHECK_EQ(victim.polls_a, 1);
  }

  // ---- #310: reused attachment numbers ---------------------------------------
  {
    one_shot<P> steady;
    one_shot<P> burst;
    small_sched_t<P> sched;
    CHECK(P::attach(sched, steady, &poll_forever<P>));
    for (int i = 0; i < 100; ++i) {
      CHECK(P::attach(sched, burst, &poll_once<P>));
      (void)sched.run_once();
      // Only the attachment that completed goes, whatever number it reused.
      CHECK(P::is_attached(sched, steady));
      CHECK(!P::is_attached(sched, burst));
    }
    CHECK_EQ(steady.polls, 100);
    CHECK_EQ(burst.polls, 100);
  }
  for (int cycles = 0; cycles <= 2 * 2; ++cycles) {
    one_shot<P> victim;
    churner<P> c;
    small_sched_t<P> sched;
    c.sched = &sched;
    c.victim = &victim;
    c.cycles = cycles;
    CHECK(P::attach(sched, c, &churn<P>));
    CHECK(P::attach(sched, victim, &poll_forever<P>));
    (void)sched.run_once();
    // Detached earlier in the round: its snapshot entry is not polled, and the
    // filler attached mid-round waits for the next round, whichever number it
    // was given.
    CHECK_EQ(victim.polls, 0);
    CHECK_EQ(c.filler.polls, 0);
    CHECK(!P::is_attached(sched, victim));
    CHECK(P::is_attached(sched, c.filler));
  }
  {
    nester<P> outer;
    one_shot<P> other;
    small_sched_t<P> sched;
    outer.sched = &sched;
    CHECK(P::attach(sched, outer, &nest<P>));
    CHECK(P::attach(sched, other, &poll_forever<P>));
    (void)sched.run_once();
    // The nested round polled `other` once and the outer round once more.
    CHECK_EQ(other.polls, 2);
    CHECK_EQ(outer.count_in_round, std::size_t{2});  // a round running counts no task more
    CHECK(!P::is_attached(sched, outer));
    CHECK(P::is_attached(sched, other));
    CHECK_EQ(sched.task_count(), std::size_t{1});  // no round's bookkeeping is left
  }
}

}  // namespace scheduler_contract
}  // namespace metl_test
