// Multi-threaded exercise of metl::atomic_handle's compare-exchange path.
//
// Runs in every configuration, and under the TSAN job it is the test that
// actually validates the memory ordering rather than just the arithmetic.
//
// The property checked is the one a lost update would break: N threads each
// perform M successful compare-exchange operations, every one of which bumps
// the stored index by exactly 1. If any update were lost, the final index would
// be below N*M. The counts are chosen so the 16-bit index never wraps.

#include "metl_check.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <metl/atomic_handle.hpp>
#include <metl/versioned_handle.hpp>

namespace {

struct tag {};
using handle = metl::versioned_handle<tag>;

constexpr int thread_count = 4;
constexpr int per_thread = 2000;
static_assert(thread_count * per_thread <= 65535, "the 16-bit index must not wrap");

void bump(metl::atomic_handle<handle>& cell, std::atomic<int>& successes) {
  for (int i = 0; i < per_thread; ++i) {
    handle observed = cell.load(std::memory_order_relaxed);
    handle desired;
    do {
      desired = handle{static_cast<std::uint16_t>(observed.index() + 1), observed.generation()};
    } while (
        !cell.compare_exchange_weak(observed, desired, std::memory_order_acq_rel, std::memory_order_relaxed));
    successes.fetch_add(1, std::memory_order_relaxed);
  }
}

}  // namespace

int main() {
  metl::atomic_handle<handle> cell{handle{0, 1}};
  std::atomic<int> successes{0};

  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (int t = 0; t < thread_count; ++t) {
    threads.emplace_back([&cell, &successes] { bump(cell, successes); });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  // Every increment landed: no lost updates.
  CHECK_EQ(successes.load(), thread_count * per_thread);
  CHECK_EQ(static_cast<int>(cell.load().index()), thread_count * per_thread);

  // The generation field rode along untouched -- a CAS on the packed word moves
  // both fields together, which is exactly why the counter cannot be skewed
  // relative to the slot it belongs to.
  CHECK_EQ(cell.load().generation(), std::uint16_t{1});

  return metl_test::exit_code();
}
