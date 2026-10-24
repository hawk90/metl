// Does try_call_once let a real interrupt in while the initializer runs, and
// does that interrupt get "not yet" instead of re-entering it? (#486)
//
// The initializer pends SysTick (ICSR.PENDSTSET) partway through, which is an
// interrupt arriving mid-initialization. Its handler calls try_call_once on
// the same flag. Three phases on an emulated Cortex-M:
//
//   - control: the same pend outside any lock is taken at once. If it is not,
//     the scenario does not work and the other phases prove nothing.
//   - guarded<..., irq_lock>::with, the existing way to initialize lazily
//     under a lock: the interrupt is held off until `with` returns, so for
//     the whole initializer. This is the cost try_call_once removes.
//   - try_call_once: the interrupt is taken inside the initializer, its own
//     try_call_once returns false and runs nothing, and the initializer runs
//     once. A second interrupt, after it returned, gets true.
//
// A last phase starts the initializer from the handler itself, which an ISR
// may do: it runs there, and the main loop then finds it done.
//
// Target-only, like tick_extender_irq_test: on a host there is no SysTick,
// and ARMv6-M has no VTOR to install the handler with. once_flag_test.cpp
// covers ARMv6-M's lock path without an interrupt.

#include "metl_check.hpp"

#include <cstdint>
#include <cstdio>

#include <metl/exp/once_flag.hpp>
#include <metl/lock.hpp>

#if defined(__ARM_ARCH_6M__)
#define METL_IRQ_TEST_HAS_VTOR 0
#else
#define METL_IRQ_TEST_HAS_VTOR 1
#endif

#if METL_HAS_IRQ_MASKING && METL_IRQ_TEST_HAS_VTOR

namespace {

constexpr std::uintptr_t kSCB_ICSR = 0xE000ED04u;
constexpr std::uintptr_t kSCB_VTOR = 0xE000ED08u;
constexpr std::uint32_t kPendSTSet = 1u << 26;
constexpr std::size_t kSysTickVector = 15;
constexpr std::size_t kVectorCount = 16;

volatile std::uint32_t& reg(std::uintptr_t address) noexcept {
  return *reinterpret_cast<volatile std::uint32_t*>(address);
}

// Pends SysTick; with interrupts unmasked it is taken before the next
// instruction after the barriers.
void pend_interrupt() noexcept {
  reg(kSCB_ICSR) = kPendSTSet;
  __asm__ __volatile__("dsb" ::: "memory");
  __asm__ __volatile__("isb" ::: "memory");
}

// --- what the handler does, per phase ------------------------------------------
enum class phase : std::uint32_t { control, guarded, once, isr_starts };

volatile phase g_phase = phase::control;
volatile std::uint32_t g_isr_count = 0;
volatile bool g_isr_result = false;

metl::exp::once_flag<> g_flag;
metl::exp::once_flag<> g_isr_flag;
volatile std::uint32_t g_runs = 0;
volatile std::uint32_t g_isr_flag_runs = 0;

void systick_handler() {
  switch (g_phase) {
    case phase::once:
      g_isr_result = metl::exp::try_call_once(g_flag, [] { g_runs = g_runs + 1; });
      break;
    case phase::isr_starts:
      g_isr_result = metl::exp::try_call_once(g_isr_flag, [] { g_isr_flag_runs = g_isr_flag_runs + 1; });
      break;
    case phase::control:
    case phase::guarded:
      break;
  }
  g_isr_count = g_isr_count + 1;
}

alignas(128) std::uint32_t g_vectors[kVectorCount];

void install_vector_table() noexcept {
  const auto* existing = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(reg(kSCB_VTOR)));
  for (std::size_t i = 0; i < kVectorCount; ++i) {
    g_vectors[i] = existing[i];
  }
  g_vectors[kSysTickVector] =
      static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&systick_handler)) | 1u;
  reg(kSCB_VTOR) = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_vectors[0]));
  __asm__ __volatile__("dsb" ::: "memory");
  __asm__ __volatile__("isb" ::: "memory");
}

}  // namespace

#endif  // METL_HAS_IRQ_MASKING && METL_IRQ_TEST_HAS_VTOR

int main() {
#if !METL_HAS_IRQ_MASKING
  return metl_test::skip("once_flag_irq_test", "METL_HAS_IRQ_MASKING == 0");
#elif !METL_IRQ_TEST_HAS_VTOR
  return metl_test::skip("once_flag_irq_test", "ARMv6-M has no VTOR; see irq_masking_test");
#else
  install_vector_table();
  __asm__ __volatile__("cpsie i" ::: "memory");

  // --- control: a pend outside any lock is taken at once ----------------------
  g_phase = phase::control;
  std::uint32_t before = g_isr_count;
  pend_interrupt();
  const bool taken_at_once = g_isr_count == before + 1;
  CHECK(taken_at_once);
  if (!taken_at_once) {
    std::printf("  the pended interrupt was not taken; the other phases would prove nothing\n");
    return metl_test::exit_code();
  }

  // --- guarded: the interrupt waits for the whole initializer -----------------
  g_phase = phase::guarded;
  metl::guarded<std::uint32_t, metl::irq_lock> shared;
  std::uint32_t isr_inside_guarded = 0;
  before = g_isr_count;
  shared.with([&](std::uint32_t& value) {
    pend_interrupt();
    isr_inside_guarded = g_isr_count - before;
    value = 1;
  });
  CHECK_EQ(isr_inside_guarded, std::uint32_t{0});
  CHECK_EQ(g_isr_count - before, std::uint32_t{1});  // taken once `with` returned

  // --- try_call_once: taken inside, told "not yet", initializer runs once -----
  g_phase = phase::once;
  std::uint32_t isr_inside_once = 0;
  bool isr_result_inside = true;
  before = g_isr_count;
  CHECK(metl::exp::try_call_once(g_flag, [&] {
    g_runs = g_runs + 1;
    pend_interrupt();
    isr_inside_once = g_isr_count - before;
    isr_result_inside = g_isr_result;
  }));
  CHECK_EQ(isr_inside_once, std::uint32_t{1});
  CHECK(!isr_result_inside);
  CHECK_EQ(g_runs, std::uint32_t{1});
  CHECK(g_flag.is_done());

  before = g_isr_count;
  pend_interrupt();
  CHECK_EQ(g_isr_count - before, std::uint32_t{1});
  CHECK(g_isr_result);  // done now
  CHECK_EQ(g_runs, std::uint32_t{1});

  // --- the handler starts it -------------------------------------------------
  g_phase = phase::isr_starts;
  pend_interrupt();
  CHECK(g_isr_result);
  CHECK_EQ(g_isr_flag_runs, std::uint32_t{1});
  CHECK(g_isr_flag.is_done());
  CHECK(metl::exp::try_call_once(g_isr_flag, [] { g_isr_flag_runs = g_isr_flag_runs + 1; }));
  CHECK_EQ(g_isr_flag_runs, std::uint32_t{1});

  std::printf(
      "once_flag_irq_test: guarded held the interrupt off; try_call_once let it in and said not yet\n");
  return metl_test::exit_code();
#endif
}
