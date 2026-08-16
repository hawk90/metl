#pragma once

#include "metl/attributes.hpp"
#include "metl/compiler.hpp"

#include <atomic>
#include <cstdint>
#include <utility>

namespace metl {

/// @file
/// @brief Lock policies and the `guarded<T, Lock>` wrapper.
///
/// METL does **not** template its containers on a lock policy, and that is a
/// deliberate choice rather than an omission. A container that locks each
/// operation cannot make a *compound* operation atomic — `if (!q.full())
/// q.push(x)` is still a race when both calls lock individually — so per-operation
/// locking gives the appearance of thread safety without the substance. It also
/// hides a cost inside every call, which is the opposite of what this library
/// promises.
///
/// What actually makes concurrent code correct is an explicit critical section
/// that spans the whole compound operation. `guarded<T, Lock>` is that: it owns
/// the object, hands it out only inside a locked scope, and lets one lock cover
/// as many operations as the invariant needs.
///
/// @code
/// metl::guarded<metl::fixed_vector<int, 8>, metl::irq_lock> shared;
///
/// // main loop — the check and the push are one critical section
/// shared.with([](auto& v) {
///   if (v.size() < v.capacity()) {
///     v.push_back(42);
///   }
/// });
/// @endcode
///
/// **On single-core MCUs, masking interrupts is the only correct lock between an
/// ISR and the main loop.** A spinlock there deadlocks: the ISR preempts the
/// holder and spins forever waiting for a thread that cannot run. `irq_lock` is
/// the primitive for that case, and it is the default for `guarded`.
///
/// No `spin_lock` is provided. It would only be correct on a multi-core target,
/// and single- versus multi-core is not detectable at compile time — so, unlike
/// every other capability in this library, the wrong use could not be turned
/// into a compile error. Shipping a primitive whose misuse is both easy and
/// silent is not worth the convenience; a multi-core user can build one from
/// `metl::atomic_handle` or `std::atomic_flag` with the trade-off in view.

/// @brief 1 when `irq_lock` really masks interrupts on this target, 0 when it
///        degrades to a compiler barrier.
///
/// Real on ARM Cortex-M (the M profile). Hosted targets have no interrupts for a
/// program to mask, so `irq_lock` compiles but provides **no mutual exclusion**
/// there — it exists so the same code builds and can be unit-tested. Assert on
/// `metl::has_irq_masking` when a deployment depends on it being real.
#if defined(__ARM_ARCH_PROFILE) && (__ARM_ARCH_PROFILE == 'M')
#define METL_HAS_IRQ_MASKING 1
#else
#define METL_HAS_IRQ_MASKING 0
#endif

/// @copydoc METL_HAS_IRQ_MASKING
inline constexpr bool has_irq_masking = (METL_HAS_IRQ_MASKING != 0);

/// @brief Lock policy that does nothing. For single-threaded use, or where the
///        caller already holds a lock.
///
/// Progress guarantee: wait-free, bounded (it is empty).
struct null_lock {
  /// State handed from `lock()` to `unlock()`. Empty here.
  using state_type = unsigned char;

  METL_NODISCARD static state_type lock() noexcept { return 0; }
  static void unlock(state_type) noexcept {}
};

/// @brief Lock policy that masks interrupts for the duration of the critical
///        section — the correct lock between an ISR and the main loop on a
///        single-core MCU.
///
/// Saves and restores `PRIMASK` rather than unconditionally re-enabling
/// interrupts. That distinction is load-bearing: an `unlock()` that simply
/// enabled interrupts would re-enable them inside a caller's own critical
/// section that had deliberately disabled them — a classic and very hard to
/// find embedded bug. Because the previous state travels with the guard rather
/// than living in the lock object, critical sections nest correctly and the lock
/// itself costs zero bytes.
///
/// Progress guarantee: wait-free, bounded — two instructions on entry, one on
/// exit. The *critical section* the caller writes is what must stay bounded;
/// interrupts are blocked for its whole duration, so keep it short.
///
/// Verified by execution, not by inspection: `tests/sync/irq_masking_test.cpp`
/// runs on an emulated Cortex-M3 in the `qemu-conformance` job, fires a real
/// SysTick interrupt, and observes that the handler does not run while this lock
/// is held — after first checking that it *does* run when the lock is not held,
/// so the result cannot be satisfied by an interrupt that never fired. It also
/// covers `guarded<>` holding the mask across a whole body, and an inner release
/// not unmasking while an outer lock is held.
///
/// @warning On targets without interrupt masking (anything that is not an ARM
///          Cortex-M) this is a **compiler barrier only** and provides no mutual
///          exclusion. Check `metl::has_irq_masking`.
/// @warning Masks *all* maskable interrupts, including the highest-priority
///          ones, so it adds directly to worst-case interrupt latency.
struct irq_lock {
  /// Saved `PRIMASK` on Cortex-M; unused elsewhere.
  using state_type = std::uint32_t;

  METL_NODISCARD static state_type lock() noexcept {
#if METL_HAS_IRQ_MASKING && (defined(__GNUC__) || defined(__clang__))
    state_type previous = 0;
    __asm__ __volatile__("mrs %0, primask" : "=r"(previous)::"memory");
    __asm__ __volatile__("cpsid i" ::: "memory");
    return previous;
#else
    // No interrupts to mask: keep the compiler barrier so the critical section
    // is at least not reordered away, and document that it is not a lock.
    std::atomic_signal_fence(std::memory_order_seq_cst);
    return 0;
#endif
  }

  static void unlock(state_type previous) noexcept {
#if METL_HAS_IRQ_MASKING && (defined(__GNUC__) || defined(__clang__))
    // Restore, never blanket-enable: `previous` is 1 when the caller already had
    // interrupts disabled, and this must leave them that way.
    __asm__ __volatile__("msr primask, %0" ::"r"(previous) : "memory");
#else
    (void)previous;
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
  }
};

/// @brief RAII critical section over a lock policy.
///
/// Holds the state returned by `Lock::lock()`, so nesting works and the policy
/// itself stays stateless.
///
/// @tparam Lock A lock policy: `null_lock`, `irq_lock`, or any type with
///         `state_type`, `lock()` and `unlock(state_type)`.
template <typename Lock>
class scoped_lock {
 public:
  scoped_lock() noexcept : state_(Lock::lock()) {}
  ~scoped_lock() { Lock::unlock(state_); }

  scoped_lock(const scoped_lock&) = delete;
  scoped_lock& operator=(const scoped_lock&) = delete;
  scoped_lock(scoped_lock&&) = delete;
  scoped_lock& operator=(scoped_lock&&) = delete;

 private:
  typename Lock::state_type state_;
};

/// @brief Owns a `T` and hands it out only inside a critical section.
///
/// The alternative to templating every container on a lock policy. One `with()`
/// call can span as many operations as the invariant requires, which is what
/// correctness actually needs — a per-operation lock cannot make `if (!full())
/// push()` atomic no matter how carefully each half is locked.
///
/// There is intentionally no `get()` or `operator->`: handing out an unguarded
/// reference would make the wrapper decorative.
///
/// @tparam T Guarded value type.
/// @tparam Lock Lock policy; defaults to `irq_lock`, the correct choice for
///         ISR↔main-loop sharing on a single-core MCU.
///
/// Progress guarantee: whatever `Lock` and the caller's lambda provide. With
/// `irq_lock` and a bounded body it is blocking-bounded, which is acceptable
/// under docs/SCOPE.md; an unbounded body would not be.
///
/// @note Not copyable or movable: moving a guarded object while another context
///       may be inside `with()` cannot be made safe.
template <typename T, typename Lock = irq_lock>
class guarded {
 public:
  using value_type = T;
  using lock_type = Lock;

  constexpr guarded() = default;

  /// Constructs the guarded value in place.
  template <typename... Args>
  explicit constexpr guarded(Args&&... args) : value_(std::forward<Args>(args)...) {}

  guarded(const guarded&) = delete;
  guarded& operator=(const guarded&) = delete;
  guarded(guarded&&) = delete;
  guarded& operator=(guarded&&) = delete;

  /// Runs `fn(value)` with the lock held and returns whatever `fn` returns.
  /// @param fn Callable taking `T&`. Keep it short: with `irq_lock` this is the
  ///        window during which interrupts are masked.
  template <typename Fn>
  decltype(auto) with(Fn&& fn) {
    scoped_lock<Lock> guard;
    return std::forward<Fn>(fn)(value_);
  }

  /// @copydoc with
  template <typename Fn>
  decltype(auto) with(Fn&& fn) const {
    scoped_lock<Lock> guard;
    return std::forward<Fn>(fn)(value_);
  }

 private:
  T value_{};
};

}  // namespace metl
