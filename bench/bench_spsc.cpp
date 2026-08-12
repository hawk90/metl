// spsc_queue throughput.
//
// The two-thread case is the one that matters — it is the only configuration
// where the cached-index optimisation from #23 does anything, because the cost
// it removes is touching the other thread's cache line. The single-thread case
// is included as a floor: it measures the queue's own arithmetic with no
// coherence traffic at all, so a regression there is a regression in the code
// rather than in the machine.
//
// Threaded numbers on a shared or thermally-throttled machine are noisy, which
// is why the harness reports the spread. Treat a change as real only when the
// min/max ranges do not overlap.

#include "metl_bench.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

#include <metl/spsc_queue.hpp>

namespace {

constexpr std::size_t kCapacity = 1024;

void spsc_single_thread_round_trip(std::uint64_t iterations) {
  metl::spsc_queue<std::uint64_t, kCapacity> queue;
  std::uint64_t out = 0;
  for (std::uint64_t n = 0; n < iterations; ++n) {
    (void)queue.try_push(n);
    (void)queue.try_pop(out);
    metl_bench::do_not_optimize(out);
  }
}

// One producer, one consumer, both spinning. Returns the number of items moved
// so the harness can turn it into a rate.
std::uint64_t spsc_two_thread_throughput(std::uint64_t items) {
  metl::spsc_queue<std::uint64_t, kCapacity> queue;
  std::atomic<bool> go{false};
  std::uint64_t checksum = 0;

  std::thread consumer([&] {
    while (!go.load(std::memory_order_acquire)) {
    }
    std::uint64_t value = 0;
    std::uint64_t received = 0;
    while (received < items) {
      if (queue.try_pop(value)) {
        checksum += value;
        ++received;
      }
    }
  });

  std::thread producer([&] {
    while (!go.load(std::memory_order_acquire)) {
    }
    for (std::uint64_t i = 0; i < items; ++i) {
      while (!queue.try_push(i)) {
      }
    }
  });

  go.store(true, std::memory_order_release);
  producer.join();
  consumer.join();

  metl_bench::do_not_optimize(checksum);
  return items;
}

}  // namespace

int main(int argc, char** argv) {
  const auto cfg = metl_bench::config::from_args(argc, argv);

  metl_bench::header("spsc_queue — single thread (no coherence traffic; a floor)");
  metl_bench::run("push + pop round trip", cfg, spsc_single_thread_round_trip);

  metl_bench::header("spsc_queue — two threads (where the cached index pays)");
  // Fewer items under --quick: this scenario cannot auto-tune, so the workload
  // has to shrink explicitly for CI.
  const std::uint64_t items = (cfg.target_seconds < 0.01) ? 200'000 : 5'000'000;
  metl_bench::run_scenario(
      "producer/consumer throughput", cfg, [items] { return spsc_two_thread_throughput(items); });

  return 0;
}
