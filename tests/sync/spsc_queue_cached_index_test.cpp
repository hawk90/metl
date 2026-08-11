// Regression test for spsc_queue's cached producer/consumer indices.
//
// Each side keeps a private copy of the other side's index and only reloads the
// real one when its copy says "full" (producer) or "empty" (consumer). The copy
// is always stale-but-conservative, so the queue must behave identically to a
// version that reloads every time. The interesting cases are exactly where an
// off-by-one in the refresh condition would hide:
//
//   * the transition into full, and the first push after a single pop
//   * the transition into empty, and the first pop after a single push
//   * both of the above straddling the ring's wrap point
//
// Single-threaded on purpose: the concurrent behaviour is covered by
// spsc_queue_threaded_test, while this pins the index arithmetic where a failure
// would be deterministic rather than a race.

#include "metl_check.hpp"

#include <cstdint>

#include <metl/spsc_queue.hpp>

namespace {

constexpr std::size_t kCapacity = 4;

}  // namespace

int main() {
  // --- fill / drain, then do it again so the ring wraps ------------------------
  {
    metl::spsc_queue<std::uint32_t, kCapacity> queue;

    for (int round = 0; round < 4; ++round) {
      const std::uint32_t base = static_cast<std::uint32_t>(round * 100);

      for (std::uint32_t i = 0; i < kCapacity; ++i) {
        CHECK(queue.try_push(base + i));
      }
      // Full: the producer's cached head says so, and reloading must confirm it.
      CHECK(queue.full());
      CHECK(!queue.try_push(999));

      for (std::uint32_t i = 0; i < kCapacity; ++i) {
        std::uint32_t out = 0;
        CHECK(queue.try_pop(out));
        CHECK_EQ(out, base + i);
      }
      // Empty: the consumer's cached tail says so, and reloading must confirm it.
      CHECK(queue.empty());
      std::uint32_t drained = 0;
      CHECK(!queue.try_pop(drained));
    }
  }

  // --- one-in-one-out at the full boundary ------------------------------------
  // Every push here starts with a cached head that says "full", so every push
  // takes the refresh path. Runs well past the ring size so it crosses the wrap.
  {
    metl::spsc_queue<std::uint32_t, kCapacity> queue;
    for (std::uint32_t i = 0; i < kCapacity; ++i) {
      CHECK(queue.try_push(i));
    }

    for (std::uint32_t i = 0; i < 32; ++i) {
      std::uint32_t out = 0;
      CHECK(queue.try_pop(out));
      CHECK_EQ(out, i);
      CHECK(queue.try_push(static_cast<std::uint32_t>(kCapacity + i)));
      CHECK(queue.full());
    }
  }

  // --- one-in-one-out at the empty boundary -----------------------------------
  // Mirror image: every pop starts with a cached tail that says "empty".
  {
    metl::spsc_queue<std::uint32_t, kCapacity> queue;
    for (std::uint32_t i = 0; i < 32; ++i) {
      CHECK(queue.empty());
      CHECK(queue.try_push(i));
      std::uint32_t out = 0;
      CHECK(queue.try_pop(out));
      CHECK_EQ(out, i);
    }
    CHECK(queue.empty());
  }

  // --- partial fills across the wrap ------------------------------------------
  // Leaves the two indices out of phase with the ring so a refresh that used the
  // masked index instead of the monotonic one would show up here.
  {
    metl::spsc_queue<std::uint32_t, kCapacity> queue;
    std::uint32_t next_push = 0;
    std::uint32_t next_pop = 0;

    for (int round = 0; round < 16; ++round) {
      const std::size_t burst = (round % 3) + 1;  // 1, 2, 3, 1, ...
      for (std::size_t i = 0; i < burst; ++i) {
        CHECK(queue.try_push(next_push++));
      }
      for (std::size_t i = 0; i < burst; ++i) {
        std::uint32_t out = 0;
        CHECK(queue.try_pop(out));
        CHECK_EQ(out, next_pop++);
      }
    }
    CHECK(queue.empty());
    CHECK_EQ(next_push, next_pop);
  }

  return metl_test::exit_code();
}
