// The ISR-shared types are constant-initialized (#263). A global queue that a
// driver's interrupt can reach before main() must already be valid when the
// first interrupt fires: if it is dynamically initialized instead, its
// constructor runs later -- in link order -- and zeroes the indices, losing
// whatever the interrupt pushed. This program is three translation units, linked
// with the pushing one first: init_order_early.cpp, init_order_queues.cpp, then
// this one.
#include "init_order_shared.hpp"
#include "metl_check.hpp"

#include <cstddef>

int main() {
  int value = 0;
  CHECK(g_spsc.try_pop(value));
  CHECK_EQ(value, 42);
  CHECK_EQ(g_ring.readable_size(), std::size_t{3});
  CHECK(g_handle.load() == init_order_handle(3, 5));
#if INIT_ORDER_CONTAINERS
  CHECK_EQ(g_vector.size(), std::size_t{1});
  CHECK_EQ(g_ring_buffer.size(), std::size_t{1});
  g_messages.with([](metl::static_message_queue<int, 8>& queue) {
    int message = 0;
    CHECK(queue.try_pop(message));
    CHECK_EQ(message, 9);
  });
#endif
  return metl_test::exit_code();
}
