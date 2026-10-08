#pragma once

// Widen a wrapping hardware counter into a monotonic 64-bit tick.
//
// Hardware timers are 16-, 24- or 32-bit counters that roll over: a 32-bit
// cycle counter at 100 MHz wraps every 43 s, a 16-bit timer at 1 MHz every
// 65 ms. `coro::deadline_scheduler` refuses a wrapping tick (a heap needs a
// strict weak ordering, which wrap-around comparison is not) and tells the
// caller to widen the counter first. This is that widening.
//
// THE ARITHMETIC IS ONE LINE; THE RACE IS THE REASON THIS TYPE EXISTS. Each
// call adds `(raw - last) mod 2^Bits` to a 64-bit total. The trap is that the
// counter read and the update are two steps. If an ISR updates the total in
// between, the interrupted caller then applies a STALE raw value, which the
// modular difference reads as almost a whole period forward -- and the total
// keeps that error forever. It shows up as a clock that jumps ahead once in a
// while, under load, and never reproduces on a bench.
//
// So `now()` takes the function that READS the counter and calls it inside the
// lock, and there is deliberately no `update(raw)` overload that would let the
// read happen outside it. The read and the update cannot be split by a
// misplaced call because the API has no place to split them.
//
// What the caller still owns, because no counter-only scheme can check it:
// consecutive calls must be LESS than one period (2^Bits counts) apart. A
// missed whole period leaves the counter exactly where it was and is
// invisible. An overflow interrupt that fires once per period is not enough on
// its own -- its entry latency varies, so two of its calls can be a little more
// than a period apart. Call from something that runs more often as well (the
// main loop, or a compare interrupt at half the period).
//
// Not in scope: how accurate a tick is in seconds. Crystal tolerance, drift
// and synchronisation to an outside clock are properties of the hardware and
// the system, not of a header.

#include "metl/attributes.hpp"
#include "metl/config.hpp"
#include "metl/lock.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace metl {

/// @brief Extends a free-running `Bits`-bit up-counter into a monotonic,
///        non-wrapping 64-bit tick, safely across ISR and main-loop callers.
///
/// **Progress guarantee (docs/SCOPE.md section 1): blocking, bounded.** `now`
/// holds `Lock` for one call of the read function plus a subtraction and an
/// addition. With `irq_lock` interrupts are masked for that window, so the
/// read function must itself be bounded -- a register load, not a loop.
///
/// @code
/// // A 16-bit timer counting up at 1 MHz, read from the main loop and from the
/// // timer's own interrupt.
/// metl::tick_extender<16> ticks;
/// auto read = [] { return static_cast<std::uint16_t>(TIM3->CNT); };
///
/// std::uint64_t now_us = ticks.now(read);   // never goes backwards
/// @endcode
///
/// A down-counter (SysTick counts down) is read as `mask - value`. The epoch is
/// the first call: the first `now` returns the raw reading itself.
///
/// @tparam Bits Width of the hardware counter, 1 to 63. The counter must wrap
///         from `2^Bits - 1` to `0`; a counter that reloads at any other value
///         (SysTick with a short reload) is not free-running and does not fit.
/// @tparam Lock Lock policy shared by every caller; defaults to `irq_lock`, the
///         correct lock between an ISR and the main loop on a single core. On
///         multi-core targets, `irq_lock` does not exclude the other core.
template <unsigned Bits, typename Lock = irq_lock>
class tick_extender {
  static_assert(Bits >= 1 && Bits <= 63,
                "tick_extender Bits must be 1..63: the extended tick is 64 bits and must be wider "
                "than the counter it extends");

 public:
  /// The extended tick. At 1 GHz it wraps after 584 years.
  using tick_type = std::uint64_t;

  /// Width of the hardware counter.
  static constexpr unsigned bits = Bits;
  /// Largest raw value the counter produces, `2^Bits - 1`.
  static constexpr tick_type mask = (tick_type{1} << Bits) - 1;

  constexpr tick_extender() noexcept = default;
  ~tick_extender() = default;

  // Every caller -- main loop and ISR -- must share ONE total. A copy would fork
  // the clock, and moving one while an ISR may be inside `now` cannot be made
  // safe, so neither is offered.
  tick_extender(const tick_extender&) = delete;
  tick_extender& operator=(const tick_extender&) = delete;
  tick_extender(tick_extender&&) = delete;
  tick_extender& operator=(tick_extender&&) = delete;

  /// @brief Read the counter and return the extended tick.
  ///
  /// @param read Callable returning the raw counter value as an unsigned
  ///        integer. Called exactly once, with the lock held.
  /// @return The extended tick. Never smaller than any value returned before,
  ///         provided calls are less than one period apart (see the header
  ///         note).
  /// @pre `read()` returns at most `mask`. A larger value means `Bits` does not
  ///      match the counter, and asserts.
  ///
  /// @note noexcept exactly when `read()` is. A read that throws leaves the
  ///       total untouched and the lock released.
  template <typename Read>
  METL_NODISCARD tick_type now(Read&& read) noexcept(noexcept(std::declval<Read>()())) {
    // Reference stripped as well as cv: a register accessor commonly returns
    // `volatile std::uint32_t&`, and that is an unsigned reading.
    using raw_type = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<Read>(read)())>>;
    static_assert(
        std::is_integral_v<raw_type> && std::is_unsigned_v<raw_type> && !std::is_same_v<raw_type, bool>,
        "tick_extender read() must return an unsigned integer: a hardware counter is "
        "unsigned, and a signed result would sign-extend past the counter's width");
    // The other mismatch is silent at run time, so it is refused here. A reading
    // narrower than Bits wraps at its own width, and each of those wraps would be
    // taken as a step of nearly 2^Bits -- the clock leaps ahead with no assert,
    // because every value it sees is within `mask`.
    static_assert(
        !std::is_unsigned_v<raw_type> || static_cast<int>(Bits) <= std::numeric_limits<raw_type>::digits,
        "tick_extender Bits is wider than the type read() returns: that reading wraps "
        "before 2^Bits, and every wrap would read as a jump of nearly a whole period");

    scoped_lock<Lock> guard;
    const auto raw = static_cast<tick_type>(std::forward<Read>(read)());
    METL_ASSERT(raw <= mask);
    // Unsigned subtraction wraps modulo 2^64; masking takes it modulo 2^Bits,
    // which is the forward distance the counter moved, wrap included.
    total_ += (raw - total_) & mask;
    return total_;
  }

 private:
  // The low `Bits` bits of the total ARE the last raw reading, so it is not
  // stored separately -- one word, one write, nothing to fall out of step.
  tick_type total_ = 0;
};

}  // namespace metl
