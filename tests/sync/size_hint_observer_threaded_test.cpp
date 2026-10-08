// size_approx() read by a thread that is neither the producer nor the consumer.
//
// Its two loads are not a snapshot: a pop can land between them, and the plain
// `tail - head` then wrapped. A queue of four reported 18446744073709551615
// elements to an observer -- empty() false, full() false, both wrong -- in
// spsc_queue and spsc_byte_ring (mpmc_queue already clamped the top). The fix
// is detail::clamped_index_distance; tests/sync/index_distance_test.cpp checks
// it deterministically, and this checks the three queues under real races.
//
// Small capacity and many short transfers, so the indices move constantly and
// the observer's two loads straddle a pop as often as possible. Before the fix,
// both SPSC queues failed on the first run on an AArch64 development host;
// mpmc_queue did not, because it already clamped the top. A pass can only ever be probabilistic for a race; a
// FAIL is definite.

#include "metl_check.hpp"

#include <atomic>
#include <cstddef>
#include <thread>

#include <metl/mpmc_queue.hpp>
#include <metl/spsc_byte_ring.hpp>
#include <metl/spsc_queue.hpp>

namespace {

constexpr int kTransfers = 500000;

// Runs one producer, one consumer and one observer; returns the largest
// size_approx() the observer saw.
template <typename Queue, typename Push, typename Pop>
std::size_t worst_observed(Queue& queue, Push push, Pop pop) {
  std::atomic<bool> done{false};
  std::size_t worst = 0;
  std::thread observer([&] {
    while (!done.load(std::memory_order_relaxed)) {
      const std::size_t n = queue.size_approx();
      worst = n > worst ? n : worst;
      // empty() and full() are derived from the same reading; they must never
      // both claim the queue is something it cannot be.
      (void)queue.empty();
      (void)queue.full();
    }
  });
  std::thread consumer([&] {
    for (int taken = 0; taken < kTransfers;) {
      if (pop()) {
        ++taken;
      }
    }
  });
  for (int given = 0; given < kTransfers;) {
    if (push()) {
      ++given;
    }
  }
  consumer.join();
  done.store(true, std::memory_order_relaxed);
  observer.join();
  return worst;
}

}  // namespace

int main() {
  {
    metl::spsc_queue<int, 4> queue;
    int sink = 0;
    const std::size_t worst =
        worst_observed(queue, [&] { return queue.try_push(1); }, [&] { return queue.try_pop(sink); });
    CHECK(worst <= queue.capacity());
  }
  {
    metl::spsc_byte_ring<4> ring;
    const std::byte byte{0x5A};
    const std::size_t worst = worst_observed(
        ring,
        [&] { return ring.try_write(metl::span<const std::byte>(&byte, 1)); },
        [&] {
          if (ring.readable_span().empty()) {
            return false;
          }
          ring.consume(1);
          return true;
        });
    CHECK(worst <= ring.capacity());
  }
  {
    metl::mpmc_queue<int, 4> queue;
    int sink = 0;
    const std::size_t worst =
        worst_observed(queue, [&] { return queue.try_push(1); }, [&] { return queue.try_pop(sink); });
    CHECK(worst <= queue.capacity());
  }
  return metl_test::exit_code();
}
