// Real multi-threaded exercise of the concurrency primitives, intended to be
// run under ThreadSanitizer (the TSAN CI job) as well as normally. A
// memory-ordering regression in spsc_queue (e.g. dropping the acquire/release
// fences that publish the element before the index) or in atomic_ref (e.g.
// performing non-atomic access) would surface here as a data race and/or a
// wrong result. Bounded and deterministic: fixed iteration counts, exact
// sum/order assertions.

#include <cstdint>
#include <thread>

#include <metl/atomic_ref.hpp>
#include <metl/spsc_queue.hpp>

namespace {

// One producer pushes 0..N-1; one consumer pops N values. Verifies exact FIFO
// order and the running sum. Because only the producer touches try_push and
// only the consumer touches try_pop, this is the exact single-producer /
// single-consumer contract spsc_queue supports.
int test_spsc() {
  constexpr int kN = 100000;
  metl::spsc_queue<int, 1024> queue;

  std::uint64_t consumed_sum = 0;
  bool order_ok = true;

  // The consumer writes consumed_sum/order_ok; main reads them only after
  // join(), which establishes the necessary happens-before edge.
  std::thread consumer([&] {
    int expected = 0;
    while (expected < kN) {
      int value = 0;
      if (queue.try_pop(value)) {
        if (value != expected) {
          order_ok = false;
        }
        consumed_sum += static_cast<std::uint64_t>(value);
        ++expected;
      } else {
        std::this_thread::yield();
      }
    }
  });

  for (int i = 0; i < kN; ++i) {
    while (!queue.try_push(i)) {
      std::this_thread::yield();
    }
  }

  consumer.join();

  const std::uint64_t expected_sum = static_cast<std::uint64_t>(kN - 1) * static_cast<std::uint64_t>(kN) / 2u;
  if (!order_ok) {
    return 1;
  }
  if (consumed_sum != expected_sum) {
    return 2;
  }
  return 0;
}

// Several threads concurrently fetch_add a shared integer through atomic_ref.
// The final value must equal the total number of increments; TSAN additionally
// verifies the accesses are genuinely atomic.
int test_atomic_ref() {
  constexpr int kThreads = 4;
  constexpr int kIncrements = 25000;

  alignas(metl::atomic_ref<int>::required_alignment) int counter = 0;

  std::thread workers[kThreads];
  for (int t = 0; t < kThreads; ++t) {
    workers[t] = std::thread([&counter] {
      metl::atomic_ref<int> ref(counter);
      for (int i = 0; i < kIncrements; ++i) {
        ref.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  metl::atomic_ref<int> ref(counter);
  if (ref.load() != kThreads * kIncrements) {
    return 3;
  }
  return 0;
}

}  // namespace

int main() {
  if (int rc = test_spsc()) {
    return rc;
  }
  if (int rc = test_atomic_ref()) {
    return rc;
  }
  return 0;
}
