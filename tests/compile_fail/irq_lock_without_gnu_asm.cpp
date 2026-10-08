// EXPECT-ERROR: metl::irq_lock has no PRIMASK sequence for this compiler on a Cortex-M target
//
// Models a Cortex-M build by a compiler without GNU inline assembly (IAR, for
// example). irq_lock used to compile there to a compiler barrier while
// METL_HAS_IRQ_MASKING said 1: a lock that masks nothing, on the one kind of
// target where that is a real race. Including the header must stay fine --
// the umbrella includes it -- and USING the lock must not compile.

#define __ARM_ARCH_PROFILE 'M'  // NOLINT: modelling the target
#define METL_DETAIL_HAS_GNU_ASM 0

#include <metl/lock.hpp>

static_assert(!metl::has_irq_masking, "no masking is claimed where none is emitted");
inline int control() {
  metl::guarded<int, metl::null_lock> value{1};  // other policies still work
  return value.with([](int& v) { return v; });
}

#ifdef METL_COMPILE_FAIL
inline void uses_irq_lock() {
  metl::scoped_lock<metl::irq_lock> guard;
}
#endif
