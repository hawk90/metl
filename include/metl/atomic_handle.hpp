#pragma once

#include "metl/attributes.hpp"
#include "metl/versioned_handle.hpp"

#include <atomic>
#include <type_traits>

namespace metl {

/// @file
/// @brief Atomic cell holding a `versioned_handle` — the Tier 1 primitive for
///        building ABA-free lock-free structures over a `handle_pool`.
///
/// This is the payoff of packing `{index, generation}` into one word. A
/// lock-free free-list needs its head pointer to carry a counter so a
/// compare-exchange cannot be fooled by a slot that was freed and re-allocated
/// in the interim (the ABA problem). The usual answers are a double-width CAS
/// (64-bit only, needs `cmpxchg16b`/`CASP`) or stuffing a counter into a
/// pointer's spare bits (breaks under AArch64 PAC/MTE, x86-64 LA57/LAM). A
/// handle needs neither: the counter is already in the word, and the word is 32
/// bits, so a plain single-word CAS suffices on a 32-bit MCU.
///
/// **Tier 1 — capability-gated.** Instantiating `atomic_handle` requires the
/// packed type to be *always* lock-free, which means a hardware CAS. ARMv7-M and
/// up (Cortex-M3/M4/M7) have `LDREX`/`STREX`; **ARMv6-M (Cortex-M0/M0+) does
/// not**, and GCC lowers atomic read-modify-write there to libatomic calls. On
/// such a target the `static_assert` below fires at instantiation. There is no
/// silent fallback on purpose: a lock-free algorithm that quietly becomes
/// lock-based has different progress guarantees, and METL states progress
/// guarantees (docs/SCOPE.md §1). Single-core targets without CAS should mask
/// interrupts around the critical section instead.
///
/// Including this header is safe everywhere — the capability check is on the
/// class template, not the header — so it stays in the umbrella and only the
/// instantiation is gated. Use `has_lock_free_handle_atomic_v` to branch at
/// compile time.

/// @brief True when `Handle`'s packed representation is always lock-free atomic.
///
/// The capability trait for `atomic_handle`. False on ARMv6-M (Cortex-M0/M0+),
/// which has no CAS instruction.
template <typename Handle>
struct has_lock_free_handle_atomic
    : std::integral_constant<bool, std::atomic<typename Handle::packed_type>::is_always_lock_free> {};

/// @copydoc has_lock_free_handle_atomic
template <typename Handle>
inline constexpr bool has_lock_free_handle_atomic_v = has_lock_free_handle_atomic<Handle>::value;

/// @brief Lock-free atomic cell holding a `versioned_handle`.
///
/// Stores the handle's packed word in a `std::atomic`, so every operation is a
/// single-word atomic on the underlying integer. No dynamic allocation; the
/// object is exactly as large as `Handle`.
///
/// Progress guarantees:
///
///   | Operation                       | Guarantee          |
///   |---------------------------------|--------------------|
///   | `load` / `store` / `exchange`   | wait-free, bounded |
///   | `compare_exchange_*`            | wait-free, bounded |
///   | a caller's CAS retry loop       | lock-free          |
///
/// The individual operations are wait-free; a retry loop built from them is
/// lock-free, not wait-free, because another thread can always win the race.
/// That distinction is why this is **not** usable for ISR↔main-loop
/// synchronisation on a single core: an ISR that preempts a retry loop and
/// spins waiting for it will deadlock. Mask interrupts there instead.
///
/// @tparam Handle A `versioned_handle` instantiation.
///
/// @note Not copyable or movable, like `std::atomic`.
template <typename Handle>
class atomic_handle {
  static_assert(has_lock_free_handle_atomic_v<Handle>,
                "metl::atomic_handle requires a lock-free single-word CAS for the handle's packed type. "
                "ARMv6-M (Cortex-M0/M0+) has no CAS instruction, so this target cannot provide one. "
                "Mask interrupts around the critical section instead, or check "
                "metl::has_lock_free_handle_atomic_v before instantiating.");

 public:
  using handle_type = Handle;
  using packed_type = typename Handle::packed_type;

  /// Always true — a non-lock-free instantiation fails the static_assert above.
  static constexpr bool is_always_lock_free = true;

  /// Constructs a cell holding the null handle.
  atomic_handle() noexcept : value_(packed_type{0}) {}

  /// Constructs a cell holding `handle`.
  explicit atomic_handle(Handle handle) noexcept : value_(handle.packed()) {}

  atomic_handle(const atomic_handle&) = delete;
  atomic_handle& operator=(const atomic_handle&) = delete;

  /// Atomically reads the stored handle.
  METL_NODISCARD Handle load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return Handle::from_packed(value_.load(order));
  }

  /// Atomically replaces the stored handle.
  void store(Handle desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
    value_.store(desired.packed(), order);
  }

  /// Atomically replaces the stored handle and returns the previous one.
  METL_NODISCARD Handle exchange(Handle desired,
                                 std::memory_order order = std::memory_order_seq_cst) noexcept {
    return Handle::from_packed(value_.exchange(desired.packed(), order));
  }

  /// Atomically compares and, on success, replaces the stored handle.
  /// @param expected Updated with the observed value on failure, as with
  ///        `std::atomic::compare_exchange_weak`.
  /// @note May fail spuriously; intended for retry loops.
  METL_NODISCARD bool compare_exchange_weak(Handle& expected,
                                            Handle desired,
                                            std::memory_order success = std::memory_order_seq_cst,
                                            std::memory_order failure = std::memory_order_seq_cst) noexcept {
    packed_type raw = expected.packed();
    const bool exchanged = value_.compare_exchange_weak(raw, desired.packed(), success, failure);
    if (!exchanged) {
      expected = Handle::from_packed(raw);
    }
    return exchanged;
  }

  /// @copydoc compare_exchange_weak
  /// @note Does not fail spuriously.
  METL_NODISCARD bool compare_exchange_strong(
      Handle& expected,
      Handle desired,
      std::memory_order success = std::memory_order_seq_cst,
      std::memory_order failure = std::memory_order_seq_cst) noexcept {
    packed_type raw = expected.packed();
    const bool exchanged = value_.compare_exchange_strong(raw, desired.packed(), success, failure);
    if (!exchanged) {
      expected = Handle::from_packed(raw);
    }
    return exchanged;
  }

 private:
  std::atomic<packed_type> value_;
};

}  // namespace metl
