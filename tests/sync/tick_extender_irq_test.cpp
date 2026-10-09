// Does tick_extender keep time when a real interrupt preempts the read?
//
// tests/sync/tick_extender_test.cpp reproduces the race with a simulated
// interrupt. This makes the same claim against real hardware behaviour on an
// emulated Cortex-M3: SysTick is the free-running counter AND the interrupt
// that preempts the main loop, which is the arrangement the header is for.
//
// The counter is 22 bits (SysTick LOAD = 2^22 - 1, so it wraps from 0 to the
// top). Its interrupt fires once per wrap and calls now() as an overflow ISR
// would. The main loop calls now() continuously, and once per phase makes a
// read that a wrap -- and so the ISR -- lands inside of: it reads the counter
// mid-period, then keeps reading until the counter has wrapped and moved on,
// and only then returns the first reading. That stretches the one window that
// matters, between reading the counter and updating the total, from a few
// instructions to most of a period, so the outcome is deterministic.
//
// What is measured is the step between consecutive ISR readings, which should
// be one period. With `null_lock` -- the control -- the ISR runs inside the
// window, the stale reading is applied after it, and one step comes out at two
// periods. With `irq_lock` the ISR waits until the update is done and every
// step stays at one period. If the control ever stops showing two periods, the
// scenario stopped reproducing the race and the irq_lock result proves nothing,
// so the control is checked first.
//
// Target-only, like irq_masking_test: on a host there is no SysTick, the test
// reports that and passes. ARMv6-M has no VTOR and is skipped for the same
// reason irq_masking_test gives.

#include "metl_check.hpp"

#include <cstdint>
#include <cstdio>

#include <metl/tick_extender.hpp>

#if defined(__ARM_ARCH_6M__)
#define METL_IRQ_TEST_HAS_VTOR 0
#else
#define METL_IRQ_TEST_HAS_VTOR 1
#endif

#if METL_HAS_IRQ_MASKING && METL_IRQ_TEST_HAS_VTOR

namespace {

// --- ARMv7-M system control registers ---------------------------------------
constexpr std::uintptr_t kSCB_VTOR = 0xE000ED08u;
constexpr std::uintptr_t kSysTick_CTRL = 0xE000E010u;
constexpr std::uintptr_t kSysTick_LOAD = 0xE000E014u;
constexpr std::uintptr_t kSysTick_VAL = 0xE000E018u;

constexpr std::uint32_t kSysTickEnable = 1u << 0;
constexpr std::uint32_t kSysTickTickInt = 1u << 1;
constexpr std::uint32_t kSysTickClkSource = 1u << 2;

constexpr std::size_t kSysTickVector = 15;
constexpr std::size_t kVectorCount = 16;

// 2^22 counts: about 170 ms at the emulated 25 MHz. Long enough that a host
// hiccup under QEMU does not skip a whole period (which no counter-only scheme
// can see, and which would fail this test for a reason that is not the code's),
// short enough that both phases finish well inside the runner's timeout.
constexpr unsigned kBits = 22;
constexpr std::uint32_t kMask = (1u << kBits) - 1u;
constexpr std::uint64_t kPeriod = std::uint64_t{1} << kBits;

// Loop budgets so a broken timer fails the test instead of hanging the runner.
constexpr std::uint32_t kSpinBudget = 50000000u;

volatile std::uint32_t& reg(std::uintptr_t address) noexcept {
  return *reinterpret_cast<volatile std::uint32_t*>(address);
}

// SysTick counts DOWN; the extender wants an up-counter.
std::uint32_t read_counter() noexcept {
  return kMask - (reg(kSysTick_VAL) & kMask);
}

// --- what the ISR observes --------------------------------------------------
using locked_clock = metl::tick_extender<kBits, metl::irq_lock>;
using unlocked_clock = metl::tick_extender<kBits, metl::null_lock>;

locked_clock g_locked;
unlocked_clock g_unlocked;
volatile bool g_use_locked = true;

volatile std::uint32_t g_isr_count = 0;
volatile std::uint64_t g_isr_last = 0;
volatile std::uint64_t g_max_step = 0;
volatile std::uint64_t g_min_step = ~std::uint64_t{0};

void systick_handler() {
  const std::uint64_t now = g_use_locked ? g_locked.now(&read_counter) : g_unlocked.now(&read_counter);
  if (g_isr_count != 0) {
    const std::uint64_t step = now - g_isr_last;
    if (step > g_max_step) {
      g_max_step = step;
    }
    if (step < g_min_step) {
      g_min_step = step;
    }
  }
  g_isr_last = now;
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

void start_systick() noexcept {
  reg(kSysTick_LOAD) = kMask;
  reg(kSysTick_VAL) = 0u;
  reg(kSysTick_CTRL) = kSysTickEnable | kSysTickTickInt | kSysTickClkSource;
}

// The read a wrap lands inside: take the reading, then wait for the counter to
// wrap and move on before handing it back. From the reading to the return is
// (period - first) + kAfterWrap counts, under one period because the reading is
// taken in the second half. Sets *budget_ok to false if the counter stalled.
constexpr std::uint32_t kAfterWrap = 1u << 16;

struct preempted_read {
  bool* budget_ok;
  std::uint32_t* isr_during;

  std::uint32_t operator()() const noexcept {
    const std::uint32_t first = read_counter();
    const std::uint32_t isr_before = g_isr_count;
    std::uint32_t spent = 0;
    while (read_counter() >= first) {  // until it wraps
      if (++spent > kSpinBudget) {
        *budget_ok = false;
        return first;
      }
    }
    while (read_counter() < kAfterWrap) {  // and moves on
      if (++spent > kSpinBudget) {
        *budget_ok = false;
        return first;
      }
    }
    *isr_during = g_isr_count - isr_before;
    return first;
  }
};

struct phase_result {
  bool budget_ok = true;
  std::uint32_t isr_during_read = 0;  // ISRs that ran inside the read window
  std::uint64_t max_step = 0;
  std::uint64_t min_step = 0;
};

template <typename Clock>
phase_result run_phase(Clock& ext, bool use_locked) {
  phase_result result;
  auto plain = [] { return read_counter(); };

  // Reset the ISR's view with interrupts masked, so it cannot see half of it.
  {
    const auto state = metl::irq_lock::lock();
    g_use_locked = use_locked;
    g_isr_count = 0;
    g_max_step = 0;
    g_min_step = ~std::uint64_t{0};
    (void)ext.now(plain);
    metl::irq_lock::unlock(state);
  }

  // Wait for one ISR reading to measure the next step from, keeping the clock
  // fed meanwhile. Then wait for the counter to reach the second half.
  std::uint32_t spent = 0;
  while (g_isr_count < 1 || read_counter() < kMask / 2 || read_counter() > kMask / 2 + kMask / 8) {
    (void)ext.now(plain);
    if (++spent > kSpinBudget) {
      result.budget_ok = false;
      return result;
    }
  }

  (void)ext.now(preempted_read{&result.budget_ok, &result.isr_during_read});

  // Keep the main loop calling until the ISR has taken the step that spans the
  // read -- one more reading.
  const std::uint32_t target = g_isr_count + 1;
  spent = 0;
  while (g_isr_count < target) {
    (void)ext.now(plain);
    if (++spent > kSpinBudget) {
      result.budget_ok = false;
      return result;
    }
  }

  const auto state = metl::irq_lock::lock();
  result.max_step = g_max_step;
  result.min_step = g_min_step;
  metl::irq_lock::unlock(state);
  return result;
}

}  // namespace

#endif  // METL_HAS_IRQ_MASKING && METL_IRQ_TEST_HAS_VTOR

int main() {
#if !METL_HAS_IRQ_MASKING
  return metl_test::skip("tick_extender_irq_test", "METL_HAS_IRQ_MASKING == 0");
#elif !METL_IRQ_TEST_HAS_VTOR
  return metl_test::skip("tick_extender_irq_test", "ARMv6-M has no VTOR; see irq_masking_test");
#else
  install_vector_table();
  __asm__ __volatile__("cpsie i" ::: "memory");
  start_systick();

  // --- control: without the lock, the race happens ---------------------------
  const phase_result control = run_phase(g_unlocked, false);
  CHECK(control.budget_ok);
  CHECK(control.isr_during_read > 0);                // the ISR really ran inside the window
  CHECK(control.max_step >= kPeriod + kPeriod / 2);  // and a period was gained
  if (!control.budget_ok || control.isr_during_read == 0 || control.max_step < kPeriod + kPeriod / 2) {
    std::printf("  the control did not reproduce the race; the irq_lock result would prove nothing\n");
    return metl_test::exit_code();
  }

  // --- the claim: with irq_lock, every step is one period ---------------------
  const phase_result locked = run_phase(g_locked, true);
  CHECK(locked.budget_ok);
  CHECK_EQ(locked.isr_during_read, std::uint32_t{0});  // masked for the whole window
  CHECK(locked.max_step < kPeriod + kPeriod / 2);
  CHECK(locked.min_step > kPeriod / 2);

  std::printf("tick_extender_irq_test: stale read gained a period without the lock, none with it\n");

  return metl_test::exit_code();
#endif
}
