// driver_registry/spi.cpp
//
// A driver translation unit of examples/driver_registry.cpp. Nothing outside it
// names the driver: linking this file in is what registers it.

#include "registry.hpp"

namespace {

int probe_spi() noexcept {
  return 0x5B;
}

registry::driver g_spi{"spi", &probe_spi};
registry::registrar g_spi_registered{g_spi};  // after g_spi: destroyed before it

}  // namespace
