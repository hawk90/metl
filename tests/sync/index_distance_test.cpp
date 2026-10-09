// detail::clamped_index_distance: the [0, capacity] clamp behind every queue's
// size_approx(). Deterministic, so it also runs on target; the race it exists
// for is exercised by tests/sync/size_hint_observer_threaded_test.cpp.

#include "metl_check.hpp"

#include <cstddef>
#include <limits>

#include <metl/detail/index_distance.hpp>

namespace {

using metl::detail::clamped_index_distance;
constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();

// The ordinary cases: the true distance, wrap included.
static_assert(clamped_index_distance(0, 0, 4) == 0, "empty");
static_assert(clamped_index_distance(10, 13, 4) == 3, "partly full");
static_assert(clamped_index_distance(10, 14, 4) == 4, "full");
static_assert(clamped_index_distance(kMax - 1, 2, 8) == 4, "indices wrapped between head and tail");

// What an observer can read between two moving counters.
static_assert(clamped_index_distance(14, 13, 4) == 0, "tail read behind head: drained, not 2^64 - 1");
static_assert(clamped_index_distance(3, kMax, 4) == 0, "behind across the wrap");
static_assert(clamped_index_distance(10, 20, 4) == 4, "tail read past capacity: clamped");

// compare_tickets, mpmc_queue's slot test. The cases that matter are where the
// two tickets straddle half the range: a signed subtraction overflows there.
constexpr std::size_t kHalf = kMax / 2;  // PTRDIFF_MAX on every supported target
static_assert(metl::detail::compare_tickets(5, 5) == 0, "equal");
static_assert(metl::detail::compare_tickets(6, 5) > 0, "ahead");
static_assert(metl::detail::compare_tickets(4, 5) < 0, "behind");
static_assert(metl::detail::compare_tickets(kHalf, kHalf + 1) < 0, "behind across PTRDIFF_MAX");
static_assert(metl::detail::compare_tickets(kHalf + 1, kHalf) > 0, "ahead across PTRDIFF_MAX");
static_assert(metl::detail::compare_tickets(kHalf + 4, kHalf + 4) == 0, "equal past PTRDIFF_MAX");
static_assert(metl::detail::compare_tickets(kMax, 0) < 0, "behind across the wrap");
static_assert(metl::detail::compare_tickets(0, kMax) > 0, "ahead across the wrap");

}  // namespace

int main() {
  // The same cases at run time, so a target build executes them too.
  volatile std::size_t head = 14;
  volatile std::size_t tail = 13;
  CHECK_EQ(clamped_index_distance(head, tail, 4), std::size_t{0});
  head = kMax - 1;
  tail = 2;
  CHECK_EQ(clamped_index_distance(head, tail, 8), std::size_t{4});
  head = 10;
  tail = 20;
  CHECK_EQ(clamped_index_distance(head, tail, 4), std::size_t{4});

  volatile std::size_t sequence = kHalf;
  volatile std::size_t pos = kHalf + 1;
  CHECK(metl::detail::compare_tickets(sequence, pos) < 0);
  sequence = kHalf + 1;
  pos = kHalf;
  CHECK(metl::detail::compare_tickets(sequence, pos) > 0);
  return metl_test::exit_code();
}
