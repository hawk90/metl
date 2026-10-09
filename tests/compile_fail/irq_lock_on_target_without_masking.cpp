// EXPECT-ERROR: metl::irq_lock cannot mask interrupts on this target
//
// Models a target with real interrupts and no masking sequence in lock.hpp:
// ESP32 (Xtensa or RISC-V), Cortex-A/R, AVR, bare-metal RISC-V. irq_lock used
// to compile there to a compiler barrier -- the default lock of guarded and
// tick_extender masking nothing. Including the header must stay fine, and
// USING the lock must not compile.

#define METL_DETAIL_HOSTED_OS 0

#include <metl/lock.hpp>

static_assert(!metl::has_irq_masking, "no masking is claimed where none is emitted");
inline int control() {
  metl::guarded<int, metl::null_lock> value{1};  // other policies still work
  return value.with([](int& v) { return v; });
}

#ifdef METL_COMPILE_FAIL
inline int uses_the_default_lock() {
  metl::guarded<int> value{1};  // irq_lock is guarded's default
  return value.with([](int& v) { return v; });
}
#endif
