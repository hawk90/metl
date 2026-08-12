// Multi-producer / multi-consumer correctness for metl::mpmc_queue.
//
// The property that matters for a lock-free queue is conservation: every item
// pushed is received exactly once — none lost, none duplicated. Order is NOT
// checked across producers, because the queue does not promise it; only that the
// multiset of received values equals the multiset of sent ones.
//
// Under the TSAN job this is also where the acquire/release pairing on the slot
// sequence numbers gets validated, which is the part a single-threaded test
// cannot reach.

#include "metl_check.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <metl/mpmc_queue.hpp>

namespace {

constexpr int kProducers = 4;
constexpr int kConsumers = 4;
constexpr std::uint32_t kPerProducer = 20000;
constexpr std::uint32_t kTotal = kProducers * kPerProducer;

// Value encoding: producer id in the high bits, sequence in the low bits, so a
// duplicate or a lost item is identifiable rather than just a count mismatch.
constexpr std::uint32_t encode(int producer, std::uint32_t index) {
  return static_cast<std::uint32_t>(producer) * kPerProducer + index;
}

}  // namespace

int main() {
  metl::mpmc_queue<std::uint32_t, 1024> queue;

  std::atomic<bool> go{false};
  std::atomic<std::uint32_t> received_total{0};

  // One slot per possible value; each must end at exactly 1.
  std::vector<std::atomic<std::uint8_t>> seen(kTotal);
  for (auto& slot : seen) {
    slot.store(0, std::memory_order_relaxed);
  }

  std::vector<std::thread> threads;
  threads.reserve(kProducers + kConsumers);

  for (int p = 0; p < kProducers; ++p) {
    threads.emplace_back([&queue, &go, p] {
      while (!go.load(std::memory_order_acquire)) {
      }
      for (std::uint32_t i = 0; i < kPerProducer; ++i) {
        // Retry until accepted: a full queue is a legitimate transient state.
        while (!queue.try_push(encode(p, i))) {
        }
      }
    });
  }

  for (int c = 0; c < kConsumers; ++c) {
    threads.emplace_back([&queue, &go, &received_total, &seen] {
      while (!go.load(std::memory_order_acquire)) {
      }
      std::uint32_t value = 0;
      while (received_total.load(std::memory_order_relaxed) < kTotal) {
        if (queue.try_pop(value)) {
          // fetch_add rather than a plain store: if the same value were handed
          // to two consumers, the count would reach 2 and the check below fails.
          seen[value].fetch_add(1, std::memory_order_relaxed);
          received_total.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  go.store(true, std::memory_order_release);
  for (auto& thread : threads) {
    thread.join();
  }

  CHECK_EQ(received_total.load(), kTotal);

  // Conservation: every value exactly once.
  std::uint32_t missing = 0;
  std::uint32_t duplicated = 0;
  for (std::uint32_t v = 0; v < kTotal; ++v) {
    const std::uint8_t count = seen[v].load(std::memory_order_relaxed);
    if (count == 0) {
      ++missing;
    } else if (count > 1) {
      ++duplicated;
    }
  }
  CHECK_EQ(missing, std::uint32_t{0});
  CHECK_EQ(duplicated, std::uint32_t{0});

  // Nothing left behind.
  std::uint32_t leftover = 0;
  CHECK(!queue.try_pop(leftover));

  return metl_test::exit_code();
}
