// driver_registry/uart.cpp
//
// A driver translation unit of examples/driver_registry.cpp. Nothing outside it
// names the driver: linking this file in is what registers it.

#include "registry.hpp"

namespace {

int probe_uart() noexcept {
  return 0x0A;
}

registry::driver g_uart{"uart", &probe_uart};
registry::registrar g_uart_registered{g_uart};  // after g_uart: destroyed before it

}  // namespace
