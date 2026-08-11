#pragma once

#include "metl/compiler.hpp"

#include <atomic>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace metl {

/// @file
/// @brief Primitives for waiting: spin hints and the event-based idle idiom.
///
/// These are **two different things** and METL keeps them apart on purpose.
///
/// `cpu_relax()` is a *hint inside a spin loop*. It tells the core that this
/// iteration is a poll, so it can de-pipeline, yield an SMT sibling, or back off
/// the memory system. It never sleeps, and it never lowers power meaningfully.
///
/// `wait_for_event()` **stops the core** until an event arrives. On ARM that is
/// the `WFE`/`SEV` pair, and it is the real idle idiom on a microcontroller: a
/// Cortex-M spinning on `cpu_relax()` burns the same current as a busy loop,
/// because the `YIELD` instruction ARMv7-M defines as a spin hint is
/// architecturally permitted to be — and in practice is — a `NOP`.
///
/// Giving both behaviours one name would be a lie on every target: a "relax"
/// that sleeps is wrong for a low-latency spin, and a "relax" that burns power
/// is wrong for a battery-powered MCU. The caller has to choose, so the API
/// makes them.

/// @brief Spin-loop hint: this iteration is a poll, not work.
///
/// Lowers to `PAUSE` on x86, `YIELD` on ARM, `PAUSE` (Zihintpause) on RISC-V
/// where available, and a compiler barrier elsewhere. Never sleeps and never
/// blocks; the loop keeps running at full speed.
///
/// Includes a compiler barrier in every configuration, so a spin loop around a
/// `volatile` or atomic load cannot be hoisted out by the optimizer even on
/// targets with no relax instruction at all.
///
/// Progress guarantee: wait-free, bounded — a single instruction or nothing.
/// Safe in ISR context.
///
/// @warning On Cortex-M this is effectively a `NOP` and saves no power. Use
///          `wait_for_event()` when the goal is to idle rather than to poll.
METL_FORCE_INLINE void cpu_relax() noexcept {
#if defined(_MSC_VER)
#if defined(_M_IX86) || defined(_M_X64)
  _mm_pause();
#elif defined(_M_ARM) || defined(_M_ARM64)
  __yield();
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
#elif defined(__i386__) || defined(__x86_64__)
  __asm__ __volatile__("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__) || defined(__thumb__)
  // YIELD is a hint in both A-profile and M-profile. On Cortex-M it is a NOP in
  // practice — see the warning above.
  __asm__ __volatile__("yield" ::: "memory");
#elif defined(__riscv) && defined(__riscv_zihintpause)
  __asm__ __volatile__("pause" ::: "memory");
#else
  // No relax instruction on this target: keep the compiler barrier so the spin
  // loop still reloads its condition.
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

/// @brief Signals the event register, waking cores blocked in `wait_for_event()`.
///
/// Lowers to `SEV` on ARM. On targets with no event mechanism this is a no-op
/// (with a compiler barrier), which is correct because `wait_for_event()` there
/// degrades to a spin hint and therefore needs no wakeup.
///
/// Progress guarantee: wait-free, bounded. Safe in ISR context — signalling from
/// an ISR is the intended use.
METL_FORCE_INLINE void send_event() noexcept {
#if defined(_MSC_VER)
#if defined(_M_ARM) || defined(_M_ARM64)
  __sev();
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
#elif defined(__aarch64__) || defined(__arm__) || defined(__thumb__)
  __asm__ __volatile__("sev" ::: "memory");
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

/// @brief Stops the core until an event arrives (ARM `WFE`).
///
/// The low-power counterpart to `cpu_relax()`. On ARM the core halts until the
/// event register is set — by an interrupt, by another core's `SEV`, or by an
/// exclusive-monitor clear. On targets with no such mechanism this degrades to
/// `cpu_relax()`, so a loop written against this API stays correct everywhere;
/// only the power behaviour differs.
///
/// **Always call this in a loop that re-checks the condition.** `WFE` may return
/// spuriously, and the event register is sticky: an event that arrives before
/// the `WFE` makes it return immediately rather than being lost. The re-check is
/// what makes both cases safe:
///
/// @code
/// while (!ready()) {
///   metl::wait_for_event();   // ISR sets `ready` and calls metl::send_event()
/// }
/// @endcode
///
/// Progress guarantee: **blocking, bounded only if a wakeup is guaranteed.** The
/// caller owns that guarantee — an interrupt that never fires, or a `send_event()`
/// that never happens, leaves the core halted indefinitely.
///
/// @warning Do not use inside an ISR on a single-core target: nothing lower in
///          priority can run to produce the event, so the core halts forever.
METL_FORCE_INLINE void wait_for_event() noexcept {
#if defined(_MSC_VER)
#if defined(_M_ARM) || defined(_M_ARM64)
  __wfe();
#else
  cpu_relax();
#endif
#elif defined(__aarch64__) || defined(__arm__) || defined(__thumb__)
  __asm__ __volatile__("wfe" ::: "memory");
#else
  cpu_relax();
#endif
}

}  // namespace metl
