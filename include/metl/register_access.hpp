#pragma once

/// @file
/// @brief Progress guarantees for the register accessors (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | every function in this header | wait-free, bounded -- at most one access or fence |
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
/// @tparam T Scalar value type (integer, enum or pointer). A class type cannot be
///         copied from a volatile lvalue: its copy constructor takes `const T&`.
/// @param addr Pointer to the volatile location; must be aligned for `T`.
/// @return The value read from `addr`.
template <typename T>
METL_NODISCARD METL_FORCE_INLINE T read_once(const volatile T* addr) noexcept {
  static_assert(std::is_scalar_v<T>,
                "read_once requires a scalar type (an integer, enum or pointer): a class has no copy from a "
                "volatile lvalue");
  return *addr;
}

/// @brief Volatile MMIO write that the optimizer may not fold or elide.
/// @tparam T Scalar value type (integer, enum or pointer).
/// @param addr Pointer to the volatile location; must be aligned for `T`.
/// @param value The value to store at `addr`.
template <typename T>
METL_FORCE_INLINE void write_once(volatile T* addr, T value) noexcept {
  static_assert(std::is_scalar_v<T>,
                "write_once requires a scalar type (an integer, enum or pointer): a class has no copy from a "
                "volatile lvalue");
  *addr = value;
}

// The three hardware barriers below order device accesses, not only memory
// shared between threads, which is all std::atomic_thread_fence promises (#586).
// The fence it lowers to can leave device I/O out:
// - RISC-V: `fence rw,rw` orders main-memory reads and writes; device input and
//   output are the I and O sets, which it does not name. Linux's mb() is
//   `fence iorw,iorw` for this reason, and its smp_mb() is `fence rw,rw`.
// - ARM: `dmb ish` orders only within the inner-shareable domain, which need not
//   include a peripheral or a DMA master; `dmb sy` is the full system. On
//   M-profile, where GCC emits `dmb ish`, ARMv7-M defines only SY and runs the
//   other options as SY without promising it.
// Elsewhere (x86, MSVC, other cores) the fence stays std::atomic_thread_fence.
#if defined(__GNUC__) && defined(__riscv)
#define METL_DETAIL_IO_FENCE_FULL "fence iorw, iorw"
#define METL_DETAIL_IO_FENCE_ACQUIRE "fence ir, iorw"
#define METL_DETAIL_IO_FENCE_RELEASE "fence iorw, ow"
#elif defined(__GNUC__) && defined(__aarch64__)
#define METL_DETAIL_IO_FENCE_FULL "dmb sy"
#define METL_DETAIL_IO_FENCE_ACQUIRE "dmb ld"
#define METL_DETAIL_IO_FENCE_RELEASE "dmb sy"
#elif defined(__GNUC__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 7
#define METL_DETAIL_IO_FENCE_FULL "dmb sy"
#define METL_DETAIL_IO_FENCE_ACQUIRE "dmb sy"
#define METL_DETAIL_IO_FENCE_RELEASE "dmb sy"
#elif defined(__GNUC__) &&       \
    (defined(__ARM_ARCH_6M__) || \
     (defined(__ARM_ARCH) && __ARM_ARCH == 6 && defined(__ARM_ARCH_PROFILE) && __ARM_ARCH_PROFILE == 'M'))
#define METL_DETAIL_IO_FENCE_FULL "dmb sy"
#define METL_DETAIL_IO_FENCE_ACQUIRE "dmb sy"
#define METL_DETAIL_IO_FENCE_RELEASE "dmb sy"
#endif

/// @brief Full barrier for ordering MMIO, DMA buffers and memory: every access
///        before it is ordered before every access after it, device ones included.
///
/// `fence iorw, iorw` on RISC-V and `dmb sy` on ARM, which `std::atomic_thread_fence`
/// does not give (see above); `std::atomic_thread_fence(seq_cst)` elsewhere, an
/// `mfence`-class fence on x86. Not a DSB: it orders accesses, it does not wait
/// for them to complete.
METL_FORCE_INLINE void barrier_full() noexcept {
#ifdef METL_DETAIL_IO_FENCE_FULL
  // The I/O fence instruction is the point: no portable spelling orders device memory.
  // NOLINTNEXTLINE(portability-no-assembler)
  __asm__ __volatile__(METL_DETAIL_IO_FENCE_FULL ::: "memory");
#else
  std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
}

/// @brief Acquire barrier: later accesses, device ones included, are not
///        reordered before the reads that precede it.
///
/// `fence ir, iorw` on RISC-V, `dmb ld` on AArch64, `dmb sy` on 32-bit ARM;
/// `std::atomic_thread_fence(acquire)` elsewhere.
METL_FORCE_INLINE void barrier_acquire() noexcept {
#ifdef METL_DETAIL_IO_FENCE_ACQUIRE
  // The I/O fence instruction is the point: no portable spelling orders device memory.
  // NOLINTNEXTLINE(portability-no-assembler)
  __asm__ __volatile__(METL_DETAIL_IO_FENCE_ACQUIRE ::: "memory");
#else
  std::atomic_thread_fence(std::memory_order_acquire);
#endif
}

/// @brief Release barrier: earlier accesses, device ones included, are not
///        reordered after the writes that follow it.
///
/// `fence iorw, ow` on RISC-V, `dmb sy` on ARM;
/// `std::atomic_thread_fence(release)` elsewhere.
METL_FORCE_INLINE void barrier_release() noexcept {
#ifdef METL_DETAIL_IO_FENCE_RELEASE
  // The I/O fence instruction is the point: no portable spelling orders device memory.
  // NOLINTNEXTLINE(portability-no-assembler)
  __asm__ __volatile__(METL_DETAIL_IO_FENCE_RELEASE ::: "memory");
#else
  std::atomic_thread_fence(std::memory_order_release);
#endif
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
