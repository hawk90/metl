#pragma once

/// @file
/// @brief `metl::once_flag` and `metl::try_call_once`: run an initializer once,
/// without waiting and without masking interrupts around it.

#include "metl/attributes.hpp"
#include "metl/config.hpp"
#include "metl/detail/word_atomic.hpp"
#include "metl/lock.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>

namespace metl {
namespace detail {
struct once_flag_access;
}  // namespace detail

/// @brief The state of a one-time initialization: not started, running, done.
///
/// The problem it answers is lazy initialization that an interrupt can reach.
/// A function-local static is the usual tool, and both of its forms fail when
/// an interrupt enters it while the first initialization is still running:
/// with `-fno-threadsafe-statics` the object is constructed twice, and with
/// thread-safe statics `__cxa_guard_acquire` traps (a bare-metal libstdc++).
/// `std::call_once` waits.
/// `guarded<std::optional<T>, irq_lock>` is correct, but it masks interrupts
/// for the whole construction.
///
/// `try_call_once` never waits and never holds the lock around the
/// initializer. A caller that finds the initializer running elsewhere, an
/// interrupt that preempted it or the initializer calling itself, gets
/// `false` and decides what to do without the object.
///
/// @code
/// metl::once_flag<> g_sensor_once;   // constant-initialized
///
/// void main_loop_step() {
///   (void)metl::try_call_once(g_sensor_once, [] { sensor_calibrate(); });
/// }
///
/// void sensor_isr() {
///   if (!g_sensor_once.is_done()) {
///     return;  // not calibrated yet: drop this sample
///   }
///   ...
/// }
/// @endcode
///
/// The state is one word. `Lock` is held only to move it from "not started"
/// to "running": one load and one store, never the initializer. The default,
/// `irq_lock`, makes that correct between an ISR and the main loop on one
/// core, including ARMv6-M, which has no compare-exchange. It does not exclude
/// another core, and on a hosted build it masks nothing: there, or on a
/// multi-core part, supply a lock that excludes every caller (the RTOS's
/// critical section), as for `guarded`.
///
/// Progress guarantees (docs/SCOPE.md section 1):
///
///   | Operation                  | Guarantee                                      |
///   |----------------------------|------------------------------------------------|
///   | `is_done()`                | wait-free, O(1): one acquire load              |
///   | `try_call_once`, once done | wait-free, O(1): one acquire load              |
///   | `try_call_once`, otherwise | blocking, bounded: `Lock` for a load and store |
///
/// The one caller that starts the initializer then runs it, at its own cost.
///
/// If the initializer never returns, or leaves by an exception, the flag stays
/// "running": every later `try_call_once` returns false and runs nothing.
///
/// @tparam Lock Lock policy for the state change (`state_type`, `lock()`,
///         `unlock(state_type)`); defaults to `irq_lock`.
template <typename Lock = irq_lock>
class once_flag {
 public:
  constexpr once_flag() noexcept = default;
  once_flag(const once_flag&) = delete;
  once_flag& operator=(const once_flag&) = delete;
  once_flag(once_flag&&) = delete;
  once_flag& operator=(once_flag&&) = delete;
  ~once_flag() = default;

  /// True once an initializer has returned. Everything it wrote is then
  /// visible to the caller.
  METL_NODISCARD bool is_done() const noexcept { return state_.load(std::memory_order_acquire) == k_done; }

 private:
  friend struct detail::once_flag_access;

  static constexpr std::uint32_t k_init = 0;
  static constexpr std::uint32_t k_running = 1;
  static constexpr std::uint32_t k_done = 2;

  detail::word_atomic<std::uint32_t> state_{k_init};
};

namespace detail {

struct once_flag_access {
  /// Moves "not started" to "running" under the flag's lock, and returns the
  /// state it found.
  template <typename Lock>
  static std::uint32_t claim(once_flag<Lock>& flag) noexcept {
    const scoped_lock<Lock> guard;
    const std::uint32_t seen = flag.state_.load(std::memory_order_acquire);
    if (seen == once_flag<Lock>::k_init) {
      flag.state_.store(once_flag<Lock>::k_running, std::memory_order_relaxed);
    }
    return seen;
  }

  template <typename Lock>
  static void finish(once_flag<Lock>& flag) noexcept {
    flag.state_.store(once_flag<Lock>::k_done, std::memory_order_release);
  }

  template <typename Lock>
  static constexpr std::uint32_t k_init = once_flag<Lock>::k_init;
  template <typename Lock>
  static constexpr std::uint32_t k_done = once_flag<Lock>::k_done;
};

}  // namespace detail

/// @brief Runs `fn(args...)` if no caller has started it on `flag`, and
///        reports whether it has finished.
///
/// @return true if the initializer has returned, whether this call ran it or
///         an earlier one did. false if it is running in another context right
///         now; this call then runs nothing. `fn`'s own result is discarded.
template <typename Lock, typename Fn, typename... Args>
METL_NODISCARD bool try_call_once(once_flag<Lock>& flag, Fn&& fn, Args&&... args) {
  using access = detail::once_flag_access;
  if (flag.is_done()) {
    return true;
  }
  const std::uint32_t seen = access::claim(flag);
  // No operation produces any other value; one means the flag's memory was
  // overwritten. A debug check, as Abseil's is: an always-on one costs 32 bytes
  // of Thumb code at every call site.
  METL_DASSERT(seen <= access::k_done<Lock>);
  if (seen != access::k_init<Lock>) {
    return seen == access::k_done<Lock>;
  }
  std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
  access::finish(flag);
  return true;
}

}  // namespace metl
