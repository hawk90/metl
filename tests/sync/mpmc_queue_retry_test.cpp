// mpmc_queue's retry branches, reached on every run (#622).
//
// A producer reads its ticket from enqueue_pos_, then its slot's sequence. If
// another producer claims and publishes that ticket in between, the sequence is
// ahead of the ticket, and the producer must re-read enqueue_pos_ and retry; a
// consumer does the same on dequeue_pos_. Only a thread switch at exactly that
// point reaches these branches, so the threaded tests reached them by chance:
// tens of thousands of times a run on an 8-core laptop, and not once in one CI
// coverage run on a smaller runner.
//
// METL_DETAIL_MPMC_RACE_POINT marks that point. Defined here, it runs a whole
// second operation on the same queue there, from the same thread. That is
// exactly the interleaving "the other producer ran between the two loads": the
// interrupted operation has only read a ticket, which it holds in a local. The
// test then checks the outcome, not only that the line ran: both elements are
// stored once, in ticket order, and the interrupted operation went round its
// loop a second time.

namespace {
void race_point(const char* side) noexcept;
}  // namespace

#define METL_DETAIL_MPMC_RACE_POINT(side) race_point(#side)

#include "metl_check.hpp"

#include <cstddef>
#include <cstring>

#include <metl/mpmc_queue.hpp>

namespace {

using queue_type = metl::mpmc_queue<int, 4>;

queue_type* g_queue = nullptr;
const char* g_armed = nullptr;  // the side whose next race point runs the intruder
int g_points = 0;               // race points passed on the armed side, intruder included
bool g_intruder_ok = false;
int g_intruder_value = 0;

void race_point(const char* side) noexcept {
  if (g_armed == nullptr || std::strcmp(side, g_armed) != 0) {
    return;
  }
  ++g_points;
  if (g_points != 1) {
    return;
  }
  // The intruder: a whole operation between the interrupted one's two loads.
  if (std::strcmp(side, "push") == 0) {
    g_intruder_ok = g_queue->try_push(100);
  } else {
    g_intruder_ok = g_queue->try_pop(g_intruder_value);
  }
}

void arm(const char* side) {
  g_armed = side;
  g_points = 0;
  g_intruder_ok = false;
  g_intruder_value = 0;
}

void producer_retries(queue_type& queue) {
  arm("push");
  CHECK(queue.try_push(1));
  g_armed = nullptr;
  CHECK(g_intruder_ok);
  // Interrupted push, intruder's push, interrupted push again after re-reading
  // the ticket: three points. Without the retry it would be two.
  CHECK_EQ(g_points, 3);
  int first = 0;
  int second = 0;
  CHECK(queue.try_pop(first));
  CHECK(queue.try_pop(second));
  CHECK_EQ(first, 100);  // the intruder took the earlier ticket
  CHECK_EQ(second, 1);
  int none = 0;
  CHECK(!queue.try_pop(none));
}

void consumer_retries(queue_type& queue) {
  CHECK(queue.try_push(10));
  CHECK(queue.try_push(20));
  arm("pop");
  int out = 0;
  CHECK(queue.try_pop(out));
  g_armed = nullptr;
  CHECK(g_intruder_ok);
  CHECK_EQ(g_points, 3);
  CHECK_EQ(g_intruder_value, 10);  // the intruder took the earlier ticket
  CHECK_EQ(out, 20);
  int none = 0;
  CHECK(!queue.try_pop(none));
}

}  // namespace

int main() {
  queue_type queue;
  g_queue = &queue;
  // Several rounds, so the tickets wrap the four slots and the retries are
  // checked on a slot whose sequence has gone round more than once.
  for (int round = 0; round < 6; ++round) {
    producer_retries(queue);
    consumer_retries(queue);
  }
  return metl_test::exit_code();
}
