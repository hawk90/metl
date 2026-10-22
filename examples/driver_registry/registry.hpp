// driver_registry/registry.hpp
//
// What every driver translation unit of examples/driver_registry.cpp sees: the
// descriptor a driver publishes, the list they are linked into, and the
// registrar that links one at startup and unlinks it at exit.

#pragma once

#include <metl/exp/intrusive_forward_list.hpp>

namespace registry {

// A driver's descriptor carries the hook, so the list needs no storage of its
// own and no capacity to outgrow. The price: the hook is written when the
// descriptor is linked, so a descriptor lives in RAM, not in flash.
struct driver : metl::exp::forward_list_hook<> {
  constexpr driver(const char* driver_name, int (*driver_probe)() noexcept) noexcept
      : name(driver_name), probe(driver_probe) {}

  const char* name;
  int (*probe)() noexcept;
};

using driver_list = metl::exp::intrusive_forward_list<driver>;

// Defined in driver_registry.cpp. Its constructor is constexpr, so the list is
// constant-initialized: it is a valid empty list before any registrar in any
// translation unit runs, whatever order they run in.
extern driver_list g_drivers;

// One per driver, defined right after the driver it registers.
//
// The destructor is not optional. Objects with static storage duration in
// different translation units are destroyed at exit in an order the program
// does not choose. If a driver were destroyed while still linked, its hook would
// stop the program (METL_HARDEN, on at every hardening level). The registrar is
// defined after its driver, so it is destroyed before it, and unlinks it then:
//   * if the list is still alive, `erase` unlinks the driver;
//   * if the list was destroyed first, its destructor already unlinked every
//     driver, `is_linked()` is false, and the list is not touched.
class registrar {
 public:
  explicit registrar(driver& d) noexcept : driver_(d) { g_drivers.push_front(driver_); }
  ~registrar() {
    if (driver_.is_linked()) {
      g_drivers.erase(driver_);
    }
  }

  registrar(const registrar&) = delete;
  registrar& operator=(const registrar&) = delete;
  registrar(registrar&&) = delete;
  registrar& operator=(registrar&&) = delete;

 private:
  driver& driver_;
};

}  // namespace registry
