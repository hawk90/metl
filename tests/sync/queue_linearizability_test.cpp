// Every concurrent history of spsc_queue and mpmc_queue is linearizable (#341).
//
// The other threaded tests check end states: everything pushed is popped, in
// order per producer. This records when each operation was invoked and when
// it returned while threads run against a small queue, then checks that one
// sequential FIFO order consistent with those times explains every result
// (tests/sync/linearizability.hpp). Rounds are short so the check is
// exhaustive, the capacity is small so full and empty both happen, and every
// round is seeded: a failure prints the seed and the history.
//
// The times are not a clock's. On a weakly ordered core a store the producer
// made before reading the clock can still be invisible to a consumer that read
// the clock later, so wall-clock order called a correct spsc_queue
// non-linearizable (seen on an Apple M2). Each operation instead takes a
// ticket from one seq_cst counter as it starts and another as it ends: when
// one operation's end ticket precedes another's start ticket, the first
// synchronizes with the second, so its effects are visible to it. Nothing
// inside an operation is fenced, so a weakened order there still shows.
//
// spsc_queue is checked strictly: a failed try_push means full and a failed
// try_pop means empty. mpmc_queue documents spurious failures while another
// thread is mid-call, so only its successful operations have to line up.
//
// The checker is tested first, on histories built by hand, so a checker that
// accepts everything cannot pass this test.

#include "linearizability.hpp"
#include "metl_check.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

#include <metl/mpmc_queue.hpp>
#include <metl/spsc_queue.hpp>

namespace {

using metl_test::failures;
using metl_test::queue_op;

std::atomic<std::uint64_t> g_ticket{0};

std::uint64_t ticket() {
  return g_ticket.fetch_add(1, std::memory_order_seq_cst);
}

queue_op push(int value, bool ok, std::uint64_t call, std::uint64_t ret) {
  return queue_op{true, value, ok, call, ret};
}

queue_op pop(int value, bool ok, std::uint64_t call, std::uint64_t ret) {
  return queue_op{false, value, ok, call, ret};
}

// ---- the checker, on histories with known answers ------------------------------

void checker_accepts_and_rejects() {
  using metl_test::linearizable;
  // Sequential and correct.
  CHECK(linearizable({push(1, true, 0, 1), push(2, true, 2, 3), pop(1, true, 4, 5), pop(2, true, 6, 7)},
                     4,
                     failures::strict));
  // Overlapping pushes may linearize in either order.
  CHECK(linearizable({push(1, true, 0, 5), push(2, true, 1, 6), pop(2, true, 7, 8), pop(1, true, 9, 10)},
                     4,
                     failures::strict));
  // Popped out of order after both pushes completed.
  CHECK(!linearizable({push(1, true, 0, 1), push(2, true, 2, 3), pop(2, true, 4, 5)}, 4, failures::strict));
  // A value popped before its push was invoked.
  CHECK(!linearizable({pop(1, true, 0, 1), push(1, true, 2, 3)}, 4, failures::strict));
  // A value popped twice.
  CHECK(!linearizable({push(1, true, 0, 1), pop(1, true, 2, 3), pop(1, true, 4, 5)}, 4, failures::strict));
  // Empty although a completed push left a value: wrong strictly, fine relaxed.
  const std::vector<queue_op> empty_after_push = {
      push(1, true, 0, 1), pop(0, false, 2, 3), pop(1, true, 4, 5)};
  CHECK(!linearizable(empty_after_push, 4, failures::strict));
  CHECK(linearizable(empty_after_push, 4, failures::relaxed));
  // Full at capacity 1, then accepted after a pop.
  CHECK(linearizable({push(1, true, 0, 1), push(2, false, 2, 3), pop(1, true, 4, 5), push(2, true, 6, 7)},
                     1,
                     failures::strict));
  // A push accepted beyond capacity.
  CHECK(!linearizable({push(1, true, 0, 1), push(2, true, 2, 3)}, 1, failures::strict));
}

// ---- recorded runs ----------------------------------------------------------------------

// Each thread records its own operations; the history is their union.
struct recorder {
  std::vector<queue_op> ops;
};

void spin(std::mt19937& rng) {
  // Every read of a volatile is kept, so the loop is not folded away.
  const volatile unsigned n = rng() % 64;
  for (unsigned i = 0; i < n; ++i) {
  }
}

std::vector<queue_op> merged(std::vector<recorder>& threads) {
  std::vector<queue_op> all;
  for (recorder& r : threads) {
    all.insert(all.end(), r.ops.begin(), r.ops.end());
  }
  std::sort(all.begin(), all.end(), [](const queue_op& a, const queue_op& b) { return a.call < b.call; });
  return all;
}

void report(const char* queue, unsigned seed, const std::vector<queue_op>& history) {
  std::fprintf(stderr, "%s: round with seed %u is not linearizable:\n", queue, seed);
  const std::uint64_t base = history.empty() ? 0 : history.front().call;
  for (const queue_op& op : history) {
    std::fprintf(stderr,
                 "  [%4llu, %4llu] %s %d -> %s\n",
                 static_cast<unsigned long long>(op.call - base),
                 static_cast<unsigned long long>(op.ret - base),
                 op.is_push ? "push" : "pop ",
                 op.value,
                 op.ok ? "ok" : "failed");
  }
}

template <typename Queue>
void push_attempts(Queue& q, recorder& out, int first_value, int attempts, unsigned seed) {
  std::mt19937 rng(seed);
  for (int k = 0; k < attempts; ++k) {
    spin(rng);
    const int value = first_value + k;
    const std::uint64_t call = ticket();
    const bool ok = q.try_push(value);
    out.ops.push_back(push(value, ok, call, ticket()));
  }
}

template <typename Queue>
void pop_attempts(Queue& q, recorder& out, int attempts, unsigned seed) {
  std::mt19937 rng(seed);
  for (int k = 0; k < attempts; ++k) {
    spin(rng);
    int value = 0;
    const std::uint64_t call = ticket();
    const bool ok = q.try_pop(value);
    out.ops.push_back(pop(value, ok, call, ticket()));
  }
}

// One round: `producers` and `consumers` threads, `attempts` operations each,
// released together. Returns whether the history was linearizable.
template <typename Queue>
bool run_round(const char* name, unsigned seed, int producers, int consumers, int attempts, failures mode) {
  Queue q;
  std::vector<recorder> records(static_cast<std::size_t>(producers + consumers));
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  for (int p = 0; p < producers; ++p) {
    threads.emplace_back([&, p] {
      while (!go.load(std::memory_order_acquire)) {
      }
      push_attempts(q,
                    records[static_cast<std::size_t>(p)],
                    (p + 1) * 100,
                    attempts,
                    seed * 31u + static_cast<unsigned>(p));
    });
  }
  for (int c = 0; c < consumers; ++c) {
    threads.emplace_back([&, c] {
      while (!go.load(std::memory_order_acquire)) {
      }
      pop_attempts(q,
                   records[static_cast<std::size_t>(producers + c)],
                   attempts,
                   seed * 17u + static_cast<unsigned>(c));
    });
  }
  go.store(true, std::memory_order_release);
  for (std::thread& t : threads) {
    t.join();
  }
  // Whatever is left joins the history as pops after every other operation,
  // so a value that vanished or appeared from nowhere is seen. The final,
  // failed pop is kept too: checked strictly, it says the queue ended empty.
  recorder drain;
  for (bool ok = true; ok;) {
    int value = 0;
    const std::uint64_t call = ticket();
    ok = q.try_pop(value);
    drain.ops.push_back(pop(value, ok, call, ticket()));
  }
  records.push_back(drain);
  const std::vector<queue_op> history = merged(records);
  const bool ok = metl_test::linearizable(history, Queue::capacity(), mode);
  if (!ok) {
    report(name, seed, history);
  }
  return ok;
}

constexpr unsigned kRounds = 2000;

void spsc_histories_are_linearizable() {
  unsigned bad = 0;
  for (unsigned seed = 1; seed <= kRounds && bad == 0; ++seed) {
    bad += run_round<metl::spsc_queue<int, 4>>("spsc_queue", seed, 1, 1, 12, failures::strict) ? 0u : 1u;
  }
  CHECK_EQ(bad, 0u);
}

void mpmc_histories_are_linearizable() {
  unsigned bad = 0;
  for (unsigned seed = 1; seed <= kRounds && bad == 0; ++seed) {
    bad += run_round<metl::mpmc_queue<int, 4>>("mpmc_queue", seed, 2, 2, 7, failures::relaxed) ? 0u : 1u;
  }
  CHECK_EQ(bad, 0u);
}

}  // namespace

int main() {
  checker_accepts_and_rejects();
  spsc_histories_are_linearizable();
  mpmc_histories_are_linearizable();
  return metl_test::exit_code();
}
