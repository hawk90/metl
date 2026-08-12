#include "metl_check.hpp"

#include <cstdint>

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

}  // namespace

int main() {
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
