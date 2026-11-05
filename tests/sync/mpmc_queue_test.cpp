// Needs a lock-free compare-and-swap, which ARMv6-M lacks: on Cortex-M0 the
// capability gate must reject it (tools/run_qemu_tests.py).
// qemu: needs-cas

// The default level is FAST, which compiles METL_DASSERT out; this test checks
// a METL_DASSERT, so it runs at DEBUG unless the build chose a level.
#ifndef METL_HARDENING
#define METL_HARDENING 2
#endif

#include "assert_trip.hpp"
#include "metl_check.hpp"

#include <cstdint>
#include <cstring>
#include <utility>

#include <metl/mpmc_queue.hpp>

namespace {

struct tracker {
  static int constructions;
  static int destructions;

  tracker() noexcept : value(0) { ++constructions; }
  explicit tracker(int input) noexcept : value(input) { ++constructions; }
  tracker(const tracker& other) noexcept : value(other.value) { ++constructions; }
  tracker(tracker&& other) noexcept : value(other.value) { ++constructions; }
  tracker& operator=(const tracker&) noexcept = default;
  tracker& operator=(tracker&&) noexcept = default;
  ~tracker() { ++destructions; }

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

// A source that marks itself moved-from, and an element whose constructor from
// it is not noexcept: the case where the queue used to build the element before
// it knew a slot was free (#575).
struct source {
  int value;
  explicit source(int input) noexcept : value(input) {}
  source(source&& other) noexcept : value(other.value) { other.value = -1; }
};

struct built_from_source {
  int value;
  built_from_source(source&& from) : value(from.value) {
    from.value = -1;
  }  // NOLINT(google-explicit-constructor)
  built_from_source(built_from_source&&) noexcept = default;
  built_from_source& operator=(built_from_source&&) noexcept = default;
};

}  // namespace

// A queue whose constructor has not run yet: zeroed storage, as a
// namespace-scope queue is before dynamic initialization (#290).
using unconstructed_queue = metl::mpmc_queue<std::uint32_t, 4>;
unconstructed_queue& zeroed_queue() {
  static unconstructed_queue queue;
  std::memset(static_cast<void*>(&queue), 0, sizeof queue);
  return queue;
}

void test_use_before_construction_traps_in_debug() {
  // At METL_HARDENING_DEBUG, a push or pop on it stops; other levels compile
  // the check out. A constructed queue in use never trips it.
  const bool push_tripped = metl_test::trips([] { (void)zeroed_queue().try_push(1U); });
  CHECK_EQ(push_tripped, METL_HARDENING >= METL_HARDENING_DEBUG);
  const bool pop_tripped = metl_test::trips([] {
    std::uint32_t out = 0;
    (void)zeroed_queue().try_pop(out);
  });
  CHECK_EQ(pop_tripped, METL_HARDENING >= METL_HARDENING_DEBUG);

  static unconstructed_queue constructed;
  const bool used_tripped = metl_test::trips([] {
    for (std::uint32_t i = 0; i < 64; ++i) {
      std::uint32_t out = 0;
      (void)constructed.try_push(i);
      (void)constructed.try_push(i);
      (void)constructed.try_pop(out);
      (void)constructed.try_pop(out);
    }
  });
  CHECK(!used_tripped);
}

int main() {
  test_use_before_construction_traps_in_debug();

  // --- #575: a failed try_emplace leaves its arguments alone ------------------
  // Without exceptions the element is built in the claimed slot, so a full queue
  // never reaches the constructor. With exceptions and a constructor that can
  // throw, it is built first; SCOPE.md R1 records that exception.
  {
    metl::mpmc_queue<built_from_source, 2> queue;
    CHECK(queue.try_emplace(source{1}));
    CHECK(queue.try_emplace(source{2}));
    source kept{3};
    CHECK(!queue.try_emplace(std::move(kept)));
#if METL_NO_EXCEPTIONS
    CHECK_EQ(kept.value, 3);
#else
    static_assert(!noexcept(queue.try_emplace(std::move(kept))), "the documented exception's shape");
#endif
  }

  // --- FIFO order with a single thread -----------------------------------------
  {
    metl::mpmc_queue<std::uint32_t, 4> queue;
    CHECK(queue.empty());
    CHECK_EQ(queue.capacity(), std::size_t{4});

    for (std::uint32_t i = 0; i < 4; ++i) {
      CHECK(queue.try_push(i));
    }
    CHECK(queue.full());
    CHECK(!queue.try_push(99));  // full: must not overwrite or grow

    for (std::uint32_t i = 0; i < 4; ++i) {
      std::uint32_t out = 0;
      CHECK(queue.try_pop(out));
      CHECK_EQ(out, i);
    }
    CHECK(queue.empty());
    std::uint32_t drained = 0;
    CHECK(!queue.try_pop(drained));
  }

  // --- the ring wraps ----------------------------------------------------------
  // The sequence numbers advance by Capacity on every reuse, so a slot's counter
  // is the thing being exercised here, not the index arithmetic.
  {
    metl::mpmc_queue<std::uint32_t, 4> queue;
    std::uint32_t expected = 0;
    for (std::uint32_t round = 0; round < 64; ++round) {
      CHECK(queue.try_push(round));
      std::uint32_t out = 0;
      CHECK(queue.try_pop(out));
      CHECK_EQ(out, expected++);
    }
    CHECK(queue.empty());
  }

  // --- partial fills across the wrap -------------------------------------------
  {
    metl::mpmc_queue<std::uint32_t, 4> queue;
    std::uint32_t next_push = 0;
    std::uint32_t next_pop = 0;
    for (int round = 0; round < 32; ++round) {
      const int burst = (round % 3) + 1;
      for (int i = 0; i < burst; ++i) {
        CHECK(queue.try_push(next_push++));
      }
      for (int i = 0; i < burst; ++i) {
        std::uint32_t out = 0;
        CHECK(queue.try_pop(out));
        CHECK_EQ(out, next_pop++);
      }
    }
    CHECK_EQ(next_push, next_pop);
  }

  // --- elements are destroyed exactly once -------------------------------------
  {
    tracker::constructions = 0;
    tracker::destructions = 0;
    {
      metl::mpmc_queue<tracker, 4> queue;
      CHECK(queue.try_emplace(1));
      CHECK(queue.try_emplace(2));

      tracker out;
      CHECK(queue.try_pop(out));
      CHECK_EQ(out.value, 1);

      // The remaining element is drained by the destructor.
    }
    // Every construction (in-queue plus the moves through `out`) is matched.
    CHECK_EQ(tracker::constructions, tracker::destructions);
  }

  // --- try_push moves when given an rvalue --------------------------------------
  {
    metl::mpmc_queue<std::uint32_t, 2> queue;
    std::uint32_t value = 7;
    CHECK(queue.try_push(value));             // copy overload
    CHECK(queue.try_push(std::uint32_t{9}));  // move overload
    std::uint32_t out = 0;
    CHECK(queue.try_pop(out));
    CHECK_EQ(out, std::uint32_t{7});
    CHECK(queue.try_pop(out));
    CHECK_EQ(out, std::uint32_t{9});
  }

  return metl_test::exit_code();
}
