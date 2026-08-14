// Does irq_lock actually block an interrupt?
//
// tests/sync/lock_test.cpp reads PRIMASK back and checks that it moves as
// intended. That is necessary and not sufficient: it proves the register
// changed, not that anything was prevented from running. docs/SCOPE.md calls
// irq_lock the correct lock between an ISR and the main loop, and that is a
// claim about behaviour, so this test makes a real interrupt fire and observes
// whether it runs.
//
// The whole body is target-only. On a host there is no SysTick and nothing to
// mask, so the test reports that and passes — it is picked up by both the host
// suite and the qemu-conformance runner with no special-casing in either.
//
// Vector table: built in RAM here and installed via VTOR rather than overriding
// a handler symbol from the C runtime's table. picolibc's naming for those
// symbols is an implementation detail this test would then depend on; VTOR is
// architectural (ARMv7-M), so this stays self-contained and would work the same
// under a different libc.

#include "metl_check.hpp"

#include <cstdint>
#include <cstdio>

#include <metl/lock.hpp>

#if METL_HAS_IRQ_MASKING

namespace {

// --- ARMv7-M system control registers ---------------------------------------
constexpr std::uintptr_t kSCB_VTOR = 0xE000ED08u;
constexpr std::uintptr_t kSysTick_CTRL = 0xE000E010u;
constexpr std::uintptr_t kSysTick_LOAD = 0xE000E014u;
constexpr std::uintptr_t kSysTick_VAL = 0xE000E018u;

constexpr std::uint32_t kSysTickEnable = 1u << 0;     // counter on
constexpr std::uint32_t kSysTickTickInt = 1u << 1;    // raise the exception
constexpr std::uint32_t kSysTickClkSource = 1u << 2;  // processor clock

// SysTick is exception number 15.
constexpr std::size_t kSysTickVector = 15;
constexpr std::size_t kVectorCount = 16;

volatile std::uint32_t g_ticks = 0;

void systick_handler() {
  ++g_ticks;
}

// VTOR requires the table to be aligned to at least 128 bytes on ARMv7-M, and to
// a power of two no smaller than the table itself.
alignas(128) std::uint32_t g_vectors[kVectorCount];

volatile std::uint32_t& reg(std::uintptr_t address) noexcept {
  return *reinterpret_cast<volatile std::uint32_t*>(address);
}

std::uint32_t read_primask() noexcept {
  std::uint32_t value = 0;
  __asm__ __volatile__("mrs %0, primask" : "=r"(value)::"memory");
  return value;
}

// Burn time without letting the optimizer erase the loop. Long enough to span
// many SysTick periods at the reload value chosen below.
void spin(std::uint32_t iterations) noexcept {
  for (volatile std::uint32_t i = 0; i < iterations; ++i) {
  }
}

void install_vector_table() noexcept {
  const auto* existing = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(reg(kSCB_VTOR)));
  for (std::size_t i = 0; i < kVectorCount; ++i) {
    g_vectors[i] = existing[i];
  }
  // Thumb bit: a vector entry must have bit 0 set or the core faults on entry.
  g_vectors[kSysTickVector] =
      static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&systick_handler)) | 1u;

  reg(kSCB_VTOR) = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_vectors[0]));
  __asm__ __volatile__("dsb" ::: "memory");
  __asm__ __volatile__("isb" ::: "memory");
}

void start_systick() noexcept {
  // Period chosen so a spin below spans a few dozen ticks, not thousands: under
  // QEMU every exception entry costs real emulated work, and a very short period
  // slows forward progress enough to risk the runner timeout.
  reg(kSysTick_LOAD) = 9999u;
  reg(kSysTick_VAL) = 0u;
  reg(kSysTick_CTRL) = kSysTickEnable | kSysTickTickInt | kSysTickClkSource;
}

}  // namespace

#endif  // METL_HAS_IRQ_MASKING

int main() {
#if !METL_HAS_IRQ_MASKING
  // No interrupts to mask on this target; lock_test covers the fallback path.
  std::printf("irq_masking_test: skipped (METL_HAS_IRQ_MASKING == 0)\n");
#else
  install_vector_table();
  __asm__ __volatile__("cpsie i" ::: "memory");
  start_systick();

  // --- control: the interrupt must actually fire ------------------------------
  // Without this, "the count did not move while masked" would be satisfied just
  // as well by a SysTick that never worked at all — which is exactly how a test
  // like this quietly becomes decorative.
  const std::uint32_t before_control = g_ticks;
  spin(100000);
  const std::uint32_t after_control = g_ticks;
  CHECK(after_control > before_control);
  if (after_control == before_control) {
    std::printf("  SysTick never fired; the rest of this test would prove nothing\n");
    return metl_test::exit_code();
  }

  // --- the actual claim: irq_lock blocks it -----------------------------------
  {
    const auto state = metl::irq_lock::lock();
    CHECK_EQ(read_primask(), std::uint32_t{1});

    const std::uint32_t at_lock = g_ticks;
    spin(100000);  // same amount of time that just produced many ticks
    const std::uint32_t while_locked = g_ticks;

    metl::irq_lock::unlock(state);

    // The handler did not run. This is the property PRIMASK readback cannot
    // establish and the whole reason this test exists.
    CHECK_EQ(while_locked, at_lock);
  }

  // --- and it resumes afterwards ----------------------------------------------
  // A lock that blocked interrupts permanently would pass everything above.
  {
    const std::uint32_t after_unlock = g_ticks;
    spin(100000);
    CHECK(g_ticks > after_unlock);
  }

  // --- guarded<> holds the mask across the whole body -------------------------
  {
    metl::guarded<std::uint32_t, metl::irq_lock> value{0u};
    const std::uint32_t ticks_during = value.with([](std::uint32_t&) {
      const std::uint32_t start = g_ticks;
      spin(100000);
      return static_cast<std::uint32_t>(g_ticks - start);
    });
    CHECK_EQ(ticks_during, std::uint32_t{0});
  }

  // --- nesting: the inner release must not let interrupts back in -------------
  {
    const auto outer = metl::irq_lock::lock();
    {
      const auto inner = metl::irq_lock::lock();
      metl::irq_lock::unlock(inner);
    }
    const std::uint32_t at_inner_release = g_ticks;
    spin(100000);
    CHECK_EQ(g_ticks, at_inner_release);  // still masked by the outer
    metl::irq_lock::unlock(outer);
  }

  std::printf("irq_masking_test: SysTick observed blocked and resumed\n");
#endif

  return metl_test::exit_code();
}
