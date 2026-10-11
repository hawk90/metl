#pragma once

/// @file
/// @brief Progress guarantees for `metl::atomic_ref` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `load`, `store` (lock-free `T`) | wait-free, bounded |
///   | `compare_exchange_weak` (lock-free `T`) | wait-free, bounded -- one attempt |
///   | `exchange`, `compare_exchange_strong` | **lock-free**, not wait-free |
///   | `fetch_add`, `fetch_sub`, `fetch_and`, `fetch_or`, `fetch_xor` | **lock-free**, not wait-free |
///   | any operation on a non-lock-free `T` | **no METL guarantee** -- see below |
///
/// On ARMv7-M and other load-linked/store-conditional machines a single
/// `compare_exchange_weak` is one `LDREX`/`STREX` attempt with no loop: it is
/// allowed to fail spuriously, so it never retries. The other read-modify-write
/// operations must not fail spuriously, so they lower to an `LDREX`/`STREX` retry
/// loop, and the retry count is not bounded: a thread can lose the reservation
/// arbitrarily many times. Under docs/SCOPE.md section 1 that restricts them to
/// multi-core use -- never between an ISR and the main loop on a single core.
/// There, the preempting ISR's operation runs to completion (barring a
/// higher-priority interrupt), and it is the main loop's retry that fails,
/// because exception entry and return clear the exclusive monitor. Each
/// interrupt that lands inside the window costs the main loop another attempt,
/// so its progress is bounded only by the interrupt rate, not by anything this
/// header controls. Mask interrupts there instead.
///
/// When `std::atomic<T>::is_always_lock_free` is false, the standard library
/// implements the operation with an address-keyed lock pool that METL does not own
/// and cannot bound, so METL states no guarantee at all for that case rather than
/// claiming one it cannot keep. Check `is_always_lock_free` before using
/// `atomic_ref` on any path with a deadline.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/addressof.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace metl {

namespace detail {

template <typename T>
inline std::atomic<T>* atomic_ref_cast(T* ptr) noexcept {
  // The pre-C++20 backport: reinterpret an aligned trivially-copyable
  // object as a std::atomic<T>. The standard does not formally bless
  // this, but it is the technique std::atomic_ref is specified to be
  // equivalent to on every implementation that ships it. The caller checks
  // alignment with METL_HARDEN in the constructor, at every hardening level.
  return reinterpret_cast<std::atomic<T>*>(ptr);
}

// Padding bits take part in std::atomic<T>'s byte-wise compare-exchange, so two
// objects with equal members can compare unequal. std::atomic_ref compares
// value bits only; clearing padding with the compiler's builtin gets the same.
#if METL_HAS_BUILTIN(__builtin_clear_padding)
template <typename T>
inline constexpr bool atomic_ref_ignores_padding_v = !std::has_unique_object_representations_v<T>;

template <typename T>
inline void atomic_ref_clear_padding(T& value) noexcept {
  __builtin_clear_padding(metl::detail::addressof(value));
}
#else
template <typename T>
inline constexpr bool atomic_ref_ignores_padding_v = false;

template <typename T>
inline void atomic_ref_clear_padding(T& /*value*/) noexcept {}
#endif

// A compare-exchange on the object's bytes, used for every T whose bytes are
// not all value bits. Not std::atomic's:
// - libstdc++ clears the padding of `expected` but compares it with the
//   object's bytes as they are, so on an object whose padding was never
//   cleared it can never succeed;
// - for a T aligned below its size (`struct { char; short; }`) it calls the
//   generic __atomic_compare_exchange, which Clang leaves to libatomic, absent
//   on bare metal.
// The bytes go through an unsigned integer of the same size, which the
// constructor's alignment check makes a valid, lock-free atomic access.
#ifdef __GNUC__
template <std::size_t Size>
struct atomic_ref_word;
template <>
struct atomic_ref_word<1> {
  using type __attribute__((__may_alias__)) = std::uint8_t;
};
template <>
struct atomic_ref_word<2> {
  using type __attribute__((__may_alias__)) = std::uint16_t;
};
template <>
struct atomic_ref_word<4> {
  using type __attribute__((__may_alias__)) = std::uint32_t;
};
template <>
struct atomic_ref_word<8> {
  using type __attribute__((__may_alias__)) = std::uint64_t;
};

template <typename T>
METL_NODISCARD inline bool try_atomic_ref_exchange_bytes(
    T* object, T& expected, const T& desired, std::memory_order order, bool weak) noexcept {
  using word = typename atomic_ref_word<sizeof(T)>::type;
  int success = __ATOMIC_SEQ_CST;
  int failure = __ATOMIC_SEQ_CST;
  switch (order) {
    case std::memory_order_relaxed:
      success = failure = __ATOMIC_RELAXED;
      break;
    case std::memory_order_consume:
    case std::memory_order_acquire:
      success = failure = __ATOMIC_ACQUIRE;
      break;
    case std::memory_order_release:
      success = __ATOMIC_RELEASE;
      failure = __ATOMIC_RELAXED;
      break;
    case std::memory_order_acq_rel:
      success = __ATOMIC_ACQ_REL;
      failure = __ATOMIC_ACQUIRE;
      break;
    case std::memory_order_seq_cst:
      break;
  }
  word held;
  word wanted;
  std::memcpy(&held, metl::detail::addressof(expected), sizeof(T));
  std::memcpy(&wanted, metl::detail::addressof(desired), sizeof(T));
  const bool exchanged =
      __atomic_compare_exchange_n(reinterpret_cast<word*>(object), &held, wanted, weak, success, failure);
  std::memcpy(metl::detail::addressof(expected), &held, sizeof(T));
  return exchanged;
}
#else
template <typename T>
METL_NODISCARD inline bool try_atomic_ref_exchange_bytes(
    T* object, T& expected, const T& desired, std::memory_order order, bool weak) noexcept {
  return weak ? atomic_ref_cast(object)->compare_exchange_weak(expected, desired, order)
              : atomic_ref_cast(object)->compare_exchange_strong(expected, desired, order);
}
#endif

}  // namespace detail

/// @brief C++17 backport of `std::atomic_ref<T>` over an externally-owned object.
///
/// Provides atomic operations on an object it does NOT own. No dynamic allocation.
/// The referenced object must be trivially copyable, 1/2/4/8 bytes, and suitably
/// aligned; alignment is asserted at construction.
///
/// @tparam T Trivially-copyable value type (size 1, 2, 4, or 8 bytes).
/// @note All operations are atomic and thread-safe when every access to the
///       referenced object goes through an `atomic_ref`. The default memory order
///       is `std::memory_order_seq_cst`.
/// @warning The referenced object must outlive this reference, and while any
///          `atomic_ref` is in use it must not be accessed non-atomically.
/// @pre The referenced object must be aligned to `required_alignment`.
///
/// @warning **On a bare-metal target, an 8-byte `T` may not link.** ARMv7-M has
///          no 64-bit atomic instruction, so `std::atomic<T>` lowers to
///          `__atomic_load_8` / `__atomic_store_8`, and bare-metal toolchains
///          ship no libatomic to satisfy them — the failure is an undefined
///          reference at link time, not a compile error. Check
///          `is_always_lock_free` when the target may be an MCU, or keep `T`
///          to 4 bytes or fewer there.
template <typename T>
class atomic_ref {
  static_assert(std::is_trivially_copyable_v<T>, "atomic_ref requires a trivially copyable type");
  static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                "atomic_ref supports 1/2/4/8 byte types");
  // The backport reinterprets the referenced T as std::atomic<T>, so the two must
  // be layout-compatible in size — otherwise an atomic op would read/write past
  // the object. Standard libraries keep sizeof(atomic<T>) == sizeof(T) even for
  // NON-lock-free T (the lock lives in an external address-keyed pool, not in the
  // object), which is exactly why std::atomic_ref works on non-lock-free types
  // too; so we require size-compatibility, not lock-freedom. Any stricter
  // alignment std::atomic<T> demands is enforced at construction via
  // required_alignment. (is_always_lock_free is still exposed below for callers
  // that want to branch on it — e.g. avoiding a lock-pool hop on Cortex-M0.)
  static_assert(sizeof(std::atomic<T>) == sizeof(T),
                "atomic_ref requires std::atomic<T> to be the same size as T");

 public:
  using value_type = T;

  /// @brief Alignment the referenced object must satisfy for atomic access.
  static constexpr std::size_t required_alignment = alignof(std::atomic<T>);
  /// @brief True if operations on @c T are always lock-free on this platform.
  static constexpr bool is_always_lock_free = std::atomic<T>::is_always_lock_free;

  /// @brief Bind the reference to an existing object.
  /// @param obj Object to operate on atomically; must outlive this reference.
  /// @pre @c obj must be aligned to `required_alignment`.
  explicit atomic_ref(T& obj) noexcept : ptr_(detail::addressof(obj)) {
    METL_HARDEN((reinterpret_cast<std::uintptr_t>(ptr_) % required_alignment) == 0u);
  }

  atomic_ref(const atomic_ref&) noexcept = default;
  atomic_ref& operator=(const atomic_ref&) = delete;

  /// @brief Atomically read the current value.
  /// @param order Memory order for the load.
  /// @return The value observed.
  T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->load(order);
  }

  /// @brief Atomically overwrite the value.
  /// @param desired New value to store.
  /// @param order Memory order for the store.
  void store(T desired, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    detail::atomic_ref_clear_padding(desired);
    detail::atomic_ref_cast(ptr_)->store(desired, order);
  }

  /// @brief Atomically replace the value and return the previous one.
  /// @param desired New value to store.
  /// @param order Memory order for the read-modify-write.
  /// @return The value held before the exchange.
  T exchange(T desired, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    detail::atomic_ref_clear_padding(desired);
    return detail::atomic_ref_cast(ptr_)->exchange(desired, order);
  }

  /// @brief Atomic strong compare-and-exchange.
  /// @param expected In/out: expected value; updated to the actual value on failure.
  /// @param desired Value stored if the comparison succeeds.
  /// @param order Memory order for the operation.
  /// @return True on success; false if the current value did not match @c expected.
  /// @note For a `T` with padding bits, only the value bits are compared, as
  ///       `std::atomic_ref` does, where the compiler provides
  ///       `__builtin_clear_padding` (GCC 11 and later, recent Clang). Without
  ///       it (Apple clang 21, MSVC) a padding-only difference makes the
  ///       exchange fail and report the held value, as `weak` may at any time.
  bool compare_exchange_strong(T& expected,
                               T desired,
                               std::memory_order order = std::memory_order_seq_cst) const noexcept {
    if constexpr (detail::atomic_ref_ignores_padding_v<T>) {
      detail::atomic_ref_clear_padding(desired);
      T held = expected;
      detail::atomic_ref_clear_padding(held);
      const T wanted = held;
      for (;;) {
        if (detail::try_atomic_ref_exchange_bytes(ptr_, held, desired, order, false)) {
          return true;
        }
        T found = held;
        detail::atomic_ref_clear_padding(found);
        if (std::memcmp(metl::detail::addressof(found), metl::detail::addressof(wanted), sizeof(T)) != 0) {
          expected = held;
          return false;
        }
        // Only the padding differed. Retry with the bytes actually held: that
        // attempt fails only if another thread stored in between.
      }
    } else if constexpr (!std::has_unique_object_representations_v<T>) {
      return detail::try_atomic_ref_exchange_bytes(ptr_, expected, desired, order, false);
    } else {
      return detail::atomic_ref_cast(ptr_)->compare_exchange_strong(expected, desired, order);
    }
  }

  /// @brief Atomic weak compare-and-exchange (may fail spuriously).
  /// @param expected In/out: expected value; updated to the actual value on failure.
  /// @param desired Value stored if the comparison succeeds.
  /// @param order Memory order for the operation.
  /// @return True on success; false otherwise (including spurious failure).
  bool compare_exchange_weak(T& expected,
                             T desired,
                             std::memory_order order = std::memory_order_seq_cst) const noexcept {
    detail::atomic_ref_clear_padding(desired);
    if constexpr (!std::has_unique_object_representations_v<T>) {
      return detail::try_atomic_ref_exchange_bytes(ptr_, expected, desired, order, true);
    } else {
      return detail::atomic_ref_cast(ptr_)->compare_exchange_weak(expected, desired, order);
    }
  }

  // Integral-only fetch operations. SFINAE on a defaulted dependent
  // template parameter so the method only participates in overload
  // resolution when T is integral (and non-bool).

  /// @brief Atomically add and return the previous value (integral @c T only).
  /// @param arg Value to add.
  /// @param order Memory order for the read-modify-write.
  /// @return The value held before the addition.
  template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>>>
  T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->fetch_add(arg, order);
  }

  /// @brief Atomically subtract and return the previous value (integral @c T only).
  /// @param arg Value to subtract.
  /// @param order Memory order for the read-modify-write.
  /// @return The value held before the subtraction.
  template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>>>
  T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->fetch_sub(arg, order);
  }

  /// @brief Atomically bitwise-AND and return the previous value (integral @c T only).
  /// @param arg Value to AND with.
  /// @param order Memory order for the read-modify-write.
  /// @return The value held before the operation.
  template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>>>
  T fetch_and(T arg, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->fetch_and(arg, order);
  }

  /// @brief Atomically bitwise-OR and return the previous value (integral @c T only).
  /// @param arg Value to OR with.
  /// @param order Memory order for the read-modify-write.
  /// @return The value held before the operation.
  template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>>>
  T fetch_or(T arg, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->fetch_or(arg, order);
  }

  /// @brief Atomically bitwise-XOR and return the previous value (integral @c T only).
  /// @param arg Value to XOR with.
  /// @param order Memory order for the read-modify-write.
  /// @return The value held before the operation.
  template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>>>
  T fetch_xor(T arg, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->fetch_xor(arg, order);
  }

 private:
  T* ptr_;
};

}  // namespace metl
