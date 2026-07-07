#pragma once

#include "metl/compiler.hpp"

#include <atomic>
#include <type_traits>

namespace metl {

// ISO C++17 strict-aliasing-safe MMIO read/write helpers.
//
// Reads/writes go through a volatile lvalue so the compiler cannot fold,
// reorder, or eliminate them. The functions are inlined to avoid call
// overhead in interrupt and time-critical paths.
template <typename T>
METL_NODISCARD METL_FORCE_INLINE T read_once(const volatile T* addr) noexcept {
  static_assert(std::is_trivially_copyable_v<T>, "read_once requires a trivially copyable type");
  return *addr;
}

template <typename T>
METL_FORCE_INLINE void write_once(volatile T* addr, T value) noexcept {
  static_assert(std::is_trivially_copyable_v<T>, "write_once requires a trivially copyable type");
  *addr = value;
}

// Memory barriers for ordering MMIO with respect to other memory operations.
// These map to std::atomic_thread_fence and translate to the appropriate
// architecture-specific fence (DMB/DSB on ARM, mfence on x86, etc.).
METL_FORCE_INLINE void barrier_full() noexcept {
  std::atomic_thread_fence(std::memory_order_seq_cst);
}

METL_FORCE_INLINE void barrier_acquire() noexcept {
  std::atomic_thread_fence(std::memory_order_acquire);
}

METL_FORCE_INLINE void barrier_release() noexcept {
  std::atomic_thread_fence(std::memory_order_release);
}

}  // namespace metl
