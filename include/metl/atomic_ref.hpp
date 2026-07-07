#pragma once

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
template <typename T>
class atomic_ref {
  static_assert(std::is_trivially_copyable_v<T>, "atomic_ref requires a trivially copyable type");
  static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                "atomic_ref supports 1/2/4/8 byte types");

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
