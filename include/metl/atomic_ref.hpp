#pragma once

/// @file
/// @brief Progress guarantees for `metl::atomic_ref` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `load`, `store` (lock-free `T`) | wait-free, bounded |
///   | `fetch_add`, `fetch_sub`, `exchange`, `compare_exchange_*` | **lock-free**, not wait-free |
///   | any operation on a non-lock-free `T` | **no METL guarantee** -- see below |
///
/// The read-modify-write operations are lock-free rather than wait-free because
/// ARMv7-M and other load-linked/store-conditional machines implement them as an
/// `LDREX`/`STREX` retry loop, and the retry count is not bounded: a thread can
/// lose the reservation arbitrarily many times. Under docs/SCOPE.md section 1 that
/// restricts them to multi-core use -- never between an ISR and the main loop on a
/// single core, where the ISR that preempts the retry makes it spin forever.
///
/// When `std::atomic<T>::is_always_lock_free` is false, the standard library
/// implements the operation with an address-keyed lock pool that METL does not own
/// and cannot bound, so METL states no guarantee at all for that case rather than
/// claiming one it cannot keep. Check `is_always_lock_free` before using
/// `atomic_ref` on any path with a deadline.

#include "metl/config.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace metl {

namespace detail {

template <typename T>
inline std::atomic<T>* atomic_ref_cast(T* ptr) noexcept {
  // The pre-C++20 backport: reinterpret an aligned trivially-copyable
  // object as a std::atomic<T>. The standard does not formally bless
  // this, but it is the technique std::atomic_ref is specified to be
  // equivalent to on every implementation that ships it. Caller checks
  // alignment via METL_ASSERT in the constructor.
  return reinterpret_cast<std::atomic<T>*>(ptr);
}

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
///          reference at link time, not a compile error. This was found by
///          running the test suite on an emulated Cortex-M3
///          (`qemu-conformance`). Check `is_always_lock_free` when the target
///          may be an MCU, or keep `T` to 4 bytes or fewer there.
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
  explicit atomic_ref(T& obj) noexcept : ptr_(&obj) {
    METL_ASSERT((reinterpret_cast<std::uintptr_t>(ptr_) % required_alignment) == 0u);
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
    detail::atomic_ref_cast(ptr_)->store(desired, order);
  }

  /// @brief Atomically replace the value and return the previous one.
  /// @param desired New value to store.
  /// @param order Memory order for the read-modify-write.
  /// @return The value held before the exchange.
  T exchange(T desired, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->exchange(desired, order);
  }

  /// @brief Atomic strong compare-and-exchange.
  /// @param expected In/out: expected value; updated to the actual value on failure.
  /// @param desired Value stored if the comparison succeeds.
  /// @param order Memory order for the operation.
  /// @return True on success; false if the current value did not match @c expected.
  bool compare_exchange_strong(T& expected,
                               T desired,
                               std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->compare_exchange_strong(expected, desired, order);
  }

  /// @brief Atomic weak compare-and-exchange (may fail spuriously).
  /// @param expected In/out: expected value; updated to the actual value on failure.
  /// @param desired Value stored if the comparison succeeds.
  /// @param order Memory order for the operation.
  /// @return True on success; false otherwise (including spurious failure).
  bool compare_exchange_weak(T& expected,
                             T desired,
                             std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->compare_exchange_weak(expected, desired, order);
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
