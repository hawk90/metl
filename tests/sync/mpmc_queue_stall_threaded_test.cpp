// mpmc_queue with a producer stalled between claiming a slot and publishing it
// (#196).
//
// The header says a try_pop that meets a claimed-but-unpublished slot returns
// false even when a later slot is already published, and that both elements
// pop once the stalled producer publishes. The stall is reachable through the
// public API: try_emplace claims its ticket and THEN constructs the element in
// the slot, so an element constructor that blocks holds the producer exactly
// there -- claimed, not published.

#include "metl_check.hpp"

#include <atomic>
#include <thread>

#include <metl/mpmc_queue.hpp>

namespace {

std::atomic<bool> g_in_constructor{false};
std::atomic<bool> g_release{false};

struct stall_tag {};

struct item {
  int value = 0;
  item() noexcept = default;
  explicit item(int v) noexcept : value(v) {}
  // Runs inside try_emplace, after the ticket is claimed and before the slot is
  // published: signal, then wait to be let go.
  item(stall_tag, int v) noexcept : value(v) {
    g_in_constructor.store(true, std::memory_order_release);
    while (!g_release.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }
};

}  // namespace

int main() {
  metl::mpmc_queue<item, 4> queue;

  std::thread stalled([&queue] { CHECK(queue.try_emplace(stall_tag{}, 1)); });
  while (!g_in_constructor.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  // Ticket 0 is claimed and unpublished; ticket 1 is published.
  CHECK(queue.try_push(item(2)));

  item out(-1);
  for (int attempt = 0; attempt < 1000; ++attempt) {
    if (!CHECK(!queue.try_pop(out))) {
      break;
    }
  }
  CHECK_EQ(out.value, -1);
  // try_pop failing does not mean the queue is empty: two tickets are taken.
  CHECK_EQ(queue.size_approx(), std::size_t{2});
  CHECK(!queue.empty());

  g_release.store(true, std::memory_order_release);
  stalled.join();

  CHECK(queue.try_pop(out));
  CHECK_EQ(out.value, 1);
  CHECK(queue.try_pop(out));
  CHECK_EQ(out.value, 2);
  CHECK(!queue.try_pop(out));
  CHECK(queue.empty());
  return metl_test::exit_code();
}
