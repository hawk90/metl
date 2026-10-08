// tick_extender: the wrap arithmetic, and the race the type exists to close.
//
// The race is reproduced deterministically on the host with a simulated
// interrupt. `sim_irq_lock` is a lock policy that, like PRIMASK, defers an
// interrupt raised while it is held and delivers it on release; `null_lock`
// lets the interrupt run immediately, wherever it lands. Raising the interrupt
// from INSIDE the read function puts it exactly in the window between reading
// the counter and updating the total -- the window a real preemption hits once
// in a long while. tests/sync/tick_extender_irq_test.cpp makes the same claim
// against a real SysTick on an emulated Cortex-M3.

#include "metl_check.hpp"

#include <csetjmp>
#include <cstdint>

#include <metl/assert.hpp>
#include <metl/tick_extender.hpp>

namespace {

// ---------------------------------------------------------------------------
// Simulated interrupt, maskable by sim_irq_lock.
// ---------------------------------------------------------------------------

void (*g_isr)() = nullptr;
int g_mask_depth = 0;
bool g_pending = false;

void raise_irq() {
  if (g_mask_depth > 0) {
    g_pending = true;  // held off, as PRIMASK would
    return;
  }
  g_isr();
}

struct sim_irq_lock {
  using state_type = int;
  static state_type lock() noexcept {
    ++g_mask_depth;
    return 0;
  }
  static void unlock(state_type) noexcept {
    --g_mask_depth;
    if (g_mask_depth == 0 && g_pending) {
      g_pending = false;
      g_isr();
    }
  }
};

// ---------------------------------------------------------------------------
// The race: a 16-bit counter, an "overflow ISR" that calls now(), and a main
// loop whose read is preempted by that ISR just after the counter wrapped.
// ---------------------------------------------------------------------------

constexpr std::uint64_t kPeriod = std::uint64_t{1} << 16;

std::uint16_t g_counter = 0;
std::uint64_t g_isr_seen = 0;

// One extender per lock policy, shared by the "ISR" and the main loop as the
// real thing would be. Static rather than a local whose address is published,
// so nothing points at a dead stack frame afterwards. Each is used by exactly
// one scenario, so each starts at the epoch.
template <typename Lock>
metl::tick_extender<16, Lock>& shared_clock() {
  static metl::tick_extender<16, Lock> instance;
  return instance;
}

template <typename Lock>
void isr() {
  g_isr_seen = shared_clock<Lock>().now([] { return g_counter; });
}

// Returns how far the main loop's next reading is from the truth. Zero means
// the extension kept up; a whole period means the clock jumped ahead and stays
// there.
template <typename Lock>
std::uint64_t error_after_preempted_read() {
  metl::tick_extender<16, Lock>& ext = shared_clock<Lock>();
  g_isr = &isr<Lock>;
  g_mask_depth = 0;
  g_pending = false;

  g_counter = 40000;
  CHECK_EQ(ext.now([] { return g_counter; }), std::uint64_t{40000});

  // Main reads 50000; before it can update, the counter wraps and the overflow
  // ISR runs at 2000. By the time main applies it, 50000 is stale.
  (void)ext.now([] {
    const std::uint16_t raw = 50000;
    g_counter = 2000;
    raise_irq();
    return raw;
  });
  CHECK_EQ(g_isr_seen, kPeriod + 2000);  // the ISR itself saw the right time

  // The main loop's next call, a moment later. The stale 50000 is now in the
  // total's low bits, so with no lock the counter at 2100 reads as having
  // travelled from 50000 forward through a wrap: a whole period too far.
  g_counter = 2100;
  return ext.now([] { return g_counter; }) - (kPeriod + 2100);
}

// ---------------------------------------------------------------------------
// Assert capture, for the raw > mask precondition.
// ---------------------------------------------------------------------------

std::jmp_buf g_jump;
bool g_asserted = false;

void capture(const char*, const char*, int) noexcept {
  g_asserted = true;
  std::longjmp(g_jump, 1);
}

}  // namespace

int main() {
  using metl::null_lock;

  // --- epoch: the first call returns the raw reading ---------------------------
  {
    metl::tick_extender<16, null_lock> ext;
    CHECK_EQ(ext.now([] { return std::uint16_t{1234}; }), std::uint64_t{1234});
    CHECK_EQ(ext.now([] { return std::uint16_t{1234}; }), std::uint64_t{1234});
  }

  // --- one wrap ----------------------------------------------------------------
  {
    metl::tick_extender<16, null_lock> ext;
    CHECK_EQ(ext.now([] { return std::uint16_t{0xFFF0}; }), std::uint64_t{0xFFF0});
    CHECK_EQ(ext.now([] { return std::uint16_t{0x0010}; }), kPeriod + 0x10);
  }

  // --- the largest step a period allows: mask counts forward -------------------
  {
    metl::tick_extender<16, null_lock> ext;
    CHECK_EQ(ext.now([] { return std::uint16_t{5}; }), std::uint64_t{5});
    CHECK_EQ(ext.now([] { return std::uint16_t{4}; }), std::uint64_t{5} + 0xFFFF);
  }

  // --- many wraps, irregular steps, all under a period: exact ------------------
  // A linear-congruential walk so the steps are irregular but reproducible.
  {
    metl::tick_extender<12, null_lock> ext;
    std::uint64_t truth = 0;
    std::uint32_t seed = 12345;
    bool monotonic = true;
    bool exact = true;
    std::uint64_t previous = 0;
    for (int i = 0; i < 20000; ++i) {
      seed = seed * 1664525u + 1013904223u;
      truth += (seed >> 8) % 4096u;  // 0 .. period - 1
      const auto raw = static_cast<std::uint16_t>(truth & 0xFFFu);
      const std::uint64_t got = ext.now([raw] { return raw; });
      exact = exact && (got == truth);
      monotonic = monotonic && (got >= previous);
      previous = got;
    }
    CHECK(exact);
    CHECK(monotonic);
    CHECK(truth > (std::uint64_t{1} << 12) * 1000);  // it really did wrap, a lot
  }

  // --- widths at both ends -------------------------------------------------------
  {
    metl::tick_extender<1, null_lock> ext;
    CHECK_EQ(ext.now([] { return 1u; }), std::uint64_t{1});
    CHECK_EQ(ext.now([] { return 0u; }), std::uint64_t{2});
    CHECK_EQ(ext.now([] { return 1u; }), std::uint64_t{3});
  }
  {
    metl::tick_extender<32, null_lock> ext;
    CHECK_EQ(ext.now([] { return 0xFFFFFFFFu; }), std::uint64_t{0xFFFFFFFFu});
    CHECK_EQ(ext.now([] { return 1u; }), (std::uint64_t{1} << 32) + 1);
  }
  {
    metl::tick_extender<63, null_lock> ext;
    constexpr std::uint64_t top = (std::uint64_t{1} << 63) - 1;
    CHECK_EQ(ext.now([] { return top; }), top);
    CHECK_EQ(ext.now([] { return std::uint64_t{0}; }), top + 1);
  }
  static_assert(metl::tick_extender<24, null_lock>::mask == 0xFFFFFFu, "mask is 2^Bits - 1");
  static_assert(metl::tick_extender<24, null_lock>::bits == 24, "bits is the template argument");

  // --- a down-counter, read as mask - value --------------------------------------
  {
    using clock_type = metl::tick_extender<24, null_lock>;
    clock_type ext;
    std::uint32_t systick_val = 0x000010;  // counting DOWN
    auto read = [&systick_val] { return static_cast<std::uint32_t>(clock_type::mask - systick_val); };
    CHECK_EQ(ext.now(read), std::uint64_t{0xFFFFEF});
    systick_val = 0xFFFFF0;  // reloaded and counted 0x20 further
    CHECK_EQ(ext.now(read), std::uint64_t{0xFFFFEF} + 0x20);
  }

  // --- the race --------------------------------------------------------------
  // The control first: without masking, the stale read is applied after the
  // ISR's update and the ext ends up a whole period ahead. If this ever reads
  // zero, the scenario below stopped reproducing the race and proves nothing.
  CHECK_EQ(error_after_preempted_read<null_lock>(), kPeriod);
  // With the read inside the lock, the ISR waits for the update and the clock
  // stays exact.
  CHECK_EQ(error_after_preempted_read<sim_irq_lock>(), std::uint64_t{0});

  // --- a raw value wider than Bits asserts --------------------------------------
  {
    metl::set_assert_handler(&capture);
    metl::tick_extender<16, null_lock> ext;
    g_asserted = false;
    if (setjmp(g_jump) == 0) {
      (void)ext.now([] { return std::uint32_t{0x10000}; });
    }
    CHECK(g_asserted);
  }

  return metl_test::exit_code();
}
