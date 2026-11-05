#pragma once

#include "metl/detail/abi.hpp"

#include <atomic>
#include <type_traits>

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {

// ARMv6-M (Cortex-M0, M0+, M1) has no exclusive loads or stores, so Clang
// treats every atomic there as unsupported and calls __atomic_load_4 and
// __atomic_store_4 even for a relaxed word load (#314). What those resolve to
// is up to the runtime: a missing symbol, or compiler-rt's lock-based fallback,
// which an interrupt can deadlock against. GCC inlines the same operations,
// which is why only Clang showed it.
//
// An aligned word load or store is single-copy atomic on ARMv6-M, and DMB
// orders memory accesses, so load/store with acquire and release need neither.
#if defined(__ARM_ARCH_6M__) || \
    (defined(__ARM_ARCH) && __ARM_ARCH == 6 && defined(__ARM_ARCH_PROFILE) && __ARM_ARCH_PROFILE == 'M')
#define METL_DETAIL_WORD_ATOMIC_ARMV6M 1
#else
#define METL_DETAIL_WORD_ATOMIC_ARMV6M 0
#endif

#if METL_DETAIL_WORD_ATOMIC_ARMV6M
/// @brief DMB on ARMv6-M: orders the memory accesses on either side of it.
inline void word_atomic_barrier() noexcept {
  __asm__ __volatile__("dmb" ::: "memory");
}

/// @brief An atomic load of an aligned word or smaller on ARMv6-M: the access
///        is single-copy atomic, and DMB after it gives acquire and seq_cst.
///        Shared by `word_atomic` and `atomic_ref` (#585).
template <typename T>
inline T word_atomic_load(const volatile T* object, std::memory_order order) noexcept {
  const T value = *object;
  if (order != std::memory_order_relaxed) {
    word_atomic_barrier();  // acquire: later accesses stay after the load
  }
  return value;
}

/// @brief An atomic store of an aligned word or smaller on ARMv6-M: DMB before
///        it gives release, and DMB after it as well gives seq_cst.
template <typename T>
inline void word_atomic_store(volatile T* object, T value, std::memory_order order) noexcept {
  if (order != std::memory_order_relaxed) {
    word_atomic_barrier();  // release: earlier accesses stay before the store
  }
  *object = value;
  if (order == std::memory_order_seq_cst) {
    word_atomic_barrier();
  }
}
#endif

/// @brief A word-sized index shared with an interrupt or another core, with
///        only atomic `load` and `store`: what the SPSC rings need, and nothing
///        that would need a compare-exchange.
///
/// Everywhere but ARMv6-M this is `std::atomic<T>`, and it static_asserts that
/// the atomic is lock-free: an ISR-shared index must never take a lock. On
/// ARMv6-M it is a volatile word with DMB barriers, for the reason above, on
/// every compiler, so the CI builds that run on an M0 exercise this path.
///
/// Progress guarantee: wait-free, bounded (one load or store, plus barriers).
template <typename T>
class word_atomic {
  static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>, "word_atomic holds an unsigned index");
  static_assert(sizeof(T) <= sizeof(void*), "word_atomic holds at most one machine word");

 public:
  constexpr explicit word_atomic(T value) noexcept : value_(value) {}
  word_atomic(const word_atomic&) = delete;
  word_atomic& operator=(const word_atomic&) = delete;
  word_atomic(word_atomic&&) = delete;
  word_atomic& operator=(word_atomic&&) = delete;
  ~word_atomic() = default;

#if METL_DETAIL_WORD_ATOMIC_ARMV6M
  T load(std::memory_order order) const noexcept { return word_atomic_load(&value_, order); }
  void store(T value, std::memory_order order) noexcept { word_atomic_store(&value_, value, order); }

 private:
  volatile T value_;
#else
  static_assert(std::atomic<T>::is_always_lock_free,
                "an index shared with an interrupt must be lock-free; this target's std::atomic is not");

  T load(std::memory_order order) const noexcept { return value_.load(order); }
  void store(T value, std::memory_order order) noexcept { value_.store(value, order); }

 private:
  std::atomic<T> value_;
#endif
};

}  // namespace detail
METL_DETAIL_ABI_END
}  // namespace metl
