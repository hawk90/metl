#pragma once

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

}  // namespace metl
