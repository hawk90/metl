#pragma once

/// @file
/// @brief Progress guarantees for the register accessors (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | every function in this header | wait-free, bounded -- one bus access each |
///
/// Each accessor is a single `volatile` load or store with no loop and no retry.
/// As with `metl/mmio.hpp`, how long one access takes is a property of the target's
/// bus -- wait states and clock-domain bridges are not something this header can
/// bound. What it guarantees is a fixed, data-independent number of accesses.

#include "metl/compiler.hpp"

#include <atomic>
#include <type_traits>

namespace metl {

/// @brief Volatile MMIO read that the optimizer may not fold or elide.
///
/// The access goes through a volatile lvalue, so the compiler cannot fold,
/// reorder, or eliminate it. Force-inlined for interrupt/time-critical paths.
/// @tparam T Trivially copyable value type.
/// @param addr Pointer to the volatile location; must be aligned for `T`.
/// @return The value read from `addr`.
template <typename T>
METL_NODISCARD METL_FORCE_INLINE T read_once(const volatile T* addr) noexcept {
  static_assert(std::is_trivially_copyable_v<T>, "read_once requires a trivially copyable type");
  return *addr;
}

/// @brief Volatile MMIO write that the optimizer may not fold or elide.
/// @tparam T Trivially copyable value type.
/// @param addr Pointer to the volatile location; must be aligned for `T`.
/// @param value The value to store at `addr`.
template <typename T>
METL_FORCE_INLINE void write_once(volatile T* addr, T value) noexcept {
  static_assert(std::is_trivially_copyable_v<T>, "write_once requires a trivially copyable type");
  *addr = value;
}

/// @brief Full (sequentially consistent) memory barrier for ordering MMIO.
///
/// Maps to `std::atomic_thread_fence` and lowers to the target's fence
/// (DMB/DSB on ARM, mfence on x86, etc.).
METL_FORCE_INLINE void barrier_full() noexcept {
  std::atomic_thread_fence(std::memory_order_seq_cst);
}

/// @brief Acquire barrier: prevents later accesses from being reordered before it.
METL_FORCE_INLINE void barrier_acquire() noexcept {
  std::atomic_thread_fence(std::memory_order_acquire);
}

/// @brief Release barrier: prevents earlier accesses from being reordered after it.
METL_FORCE_INLINE void barrier_release() noexcept {
  std::atomic_thread_fence(std::memory_order_release);
}

/// @brief Compiler-only barrier: stops the OPTIMIZER reordering across this
///        point, and emits no instruction.
///
/// The counterpart to the three barriers above, which emit real fences. Use this
/// when the ordering that matters is between a single core and something it
/// shares state with without a second observer on the bus — an ISR on the same
/// core, or a signal handler. Those cannot see a reordering the hardware does
/// not perform, but they absolutely can see one the compiler performs.
///
/// @warning NOT sufficient between cores, or between a core and a DMA engine or
///          peripheral: no instruction is emitted, so nothing constrains the
///          hardware. Use `barrier_full()`/`barrier_acquire()`/`barrier_release()`
///          there.
METL_FORCE_INLINE void compiler_barrier() noexcept {
  std::atomic_signal_fence(std::memory_order_seq_cst);
}

}  // namespace metl
