// mpmc_queue throughput, and what it costs relative to spsc_queue.
//
// The comparison is the point. spsc_queue is wait-free per operation and touches
// no shared counter; mpmc_queue pays a compare-exchange per operation on a
// counter every thread contends for. Anyone choosing between them should be able
// to see that price rather than guess at it — and with one producer and one
// consumer, spsc is the right answer whenever the roles really are fixed.
//
// Contended numbers are noisy by nature: the harness reports the spread, and a
// difference is only real when the ranges do not overlap.

#include "metl_bench.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <metl/mpmc_queue.hpp>
#include <metl/spsc_queue.hpp>

namespace {

constexpr std::size_t kCapacity = 1024;

void mpmc_single_thread_round_trip(std::uint64_t iterations) {
  metl::mpmc_queue<std::uint64_t, kCapacity> queue;
  std::uint64_t out = 0;
  for (std::uint64_t n = 0; n < iterations; ++n) {
    (void)queue.try_push(n);
    (void)queue.try_pop(out);
    metl_bench::do_not_optimize(out);
  }
}

void spsc_single_thread_round_trip(std::uint64_t iterations) {
  metl::spsc_queue<std::uint64_t, kCapacity> queue;
  std::uint64_t out = 0;
  for (std::uint64_t n = 0; n < iterations; ++n) {
    (void)queue.try_push(n);
    (void)queue.try_pop(out);
    metl_bench::do_not_optimize(out);
  }
}

std::uint64_t mpmc_throughput(int producers, int consumers, std::uint64_t per_producer) {
  metl::mpmc_queue<std::uint64_t, kCapacity> queue;
  std::atomic<bool> go{false};
  std::atomic<std::uint64_t> received{0};
  const std::uint64_t total = per_producer * static_cast<std::uint64_t>(producers);

  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(producers + consumers));

  for (int p = 0; p < producers; ++p) {
    threads.emplace_back([&queue, &go, per_producer] {
      while (!go.load(std::memory_order_acquire)) {
      }
      for (std::uint64_t i = 0; i < per_producer; ++i) {
        while (!queue.try_push(i)) {
        }
      }
    });
  }

  for (int c = 0; c < consumers; ++c) {
    threads.emplace_back([&queue, &go, &received, total] {
      while (!go.load(std::memory_order_acquire)) {
      }
      std::uint64_t value = 0;
      while (received.load(std::memory_order_relaxed) < total) {
        if (queue.try_pop(value)) {
          received.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  go.store(true, std::memory_order_release);
  for (auto& thread : threads) {
    thread.join();
  }
  return total;
}

}  // namespace

int main(int argc, char** argv) {
  const auto cfg = metl_bench::config::from_args(argc, argv);
  const bool quick = cfg.target_seconds < 0.01;

  metl_bench::header(cfg, "single thread — the per-operation cost, uncontended");
  metl_bench::run("spsc_queue push + pop", cfg, spsc_single_thread_round_trip);
  metl_bench::run("mpmc_queue push + pop", cfg, mpmc_single_thread_round_trip);

  metl_bench::header(cfg, "contended throughput (producers x consumers)");
  const std::uint64_t per_producer = quick ? 50'000 : 1'000'000;
  metl_bench::run_scenario(
      "mpmc_queue 1p x 1c", cfg, [per_producer] { return mpmc_throughput(1, 1, per_producer); });
  metl_bench::run_scenario(
      "mpmc_queue 2p x 2c", cfg, [per_producer] { return mpmc_throughput(2, 2, per_producer / 2); });
  metl_bench::run_scenario(
      "mpmc_queue 4p x 4c", cfg, [per_producer] { return mpmc_throughput(4, 4, per_producer / 4); });

  return metl_bench::require_selection(cfg);
}
