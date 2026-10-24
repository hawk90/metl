// metl::exp::try_call_once from many threads at once (#486), with a lock
// policy that excludes them all, as a multi-core user supplies one. Run under
// ThreadSanitizer by the TSAN job: the initializer writes plain data, and every
// thread that is told "done" reads it, so a missing acquire or release on the
// state is a data race there and a wrong value anywhere.

#include "metl_check.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <metl/exp/once_flag.hpp>

namespace {

std::mutex g_mutex;

struct mutex_lock {
  using state_type = int;
  static state_type lock() noexcept {
    g_mutex.lock();
    return 0;
  }
  static void unlock(state_type) noexcept { g_mutex.unlock(); }
};

constexpr int kThreads = 8;
constexpr int kRounds = 200;

void test_one_runner_and_everyone_sees_its_writes() {
  for (int round = 0; round < kRounds; ++round) {
    metl::exp::once_flag<mutex_lock> flag;
    std::uint64_t payload = 0;  // plain: published only by the flag
    std::atomic<int> runs{0};
    std::atomic<int> false_returns{0};
    std::atomic<int> wrong_payload{0};
    std::atomic<bool> go{false};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&] {
        while (!go.load(std::memory_order_acquire)) {
        }
        // Retry until done: a thread that needs the value has to wait
        // somehow, and that wait is the caller's, not the library's.
        while (!metl::exp::try_call_once(flag, [&] {
          runs.fetch_add(1, std::memory_order_relaxed);
          payload = 0x1234'5678'9abc'def0ULL;
        })) {
          false_returns.fetch_add(1, std::memory_order_relaxed);
          std::this_thread::yield();
        }
        if (payload != 0x1234'5678'9abc'def0ULL) {
          wrong_payload.fetch_add(1, std::memory_order_relaxed);
        }
        if (!flag.is_done()) {
          wrong_payload.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
    go.store(true, std::memory_order_release);
    for (auto& thread : threads) {
      thread.join();
    }
    CHECK_EQ(runs.load(), 1);
    CHECK_EQ(wrong_payload.load(), 0);
    (void)false_returns;
  }
}

}  // namespace

int main() {
  test_one_runner_and_everyone_sees_its_writes();
  return metl_test::exit_code();
}
