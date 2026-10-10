// A translation unit whose global constructor starts a peripheral, and the
// peripheral's interrupt fires before main() -- modelled by calling the ISR
// body from the constructor. It is linked BEFORE init_order_queues.cpp, so its
// dynamic initialization runs first.
#include "init_order_shared.hpp"

#include <cstddef>

namespace {

void early_isr() {
  (void)g_spsc.try_push(42);
  const std::byte bytes[3] = {std::byte{1}, std::byte{2}, std::byte{3}};
  (void)g_ring.try_write(metl::span<const std::byte>(bytes, 3));
  g_handle.store(init_order_handle(3, 5));
}

struct early_driver {
  early_driver() { early_isr(); }
} g_early_driver;

}  // namespace
