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

// C++17 backport of std::atomic_ref<T>. Operates on an externally-owned
// trivially-copyable object that must outlive the reference. Alignment
// is checked at construction.
template <typename T>
class atomic_ref {
  static_assert(std::is_trivially_copyable_v<T>, "atomic_ref requires a trivially copyable type");
  static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                "atomic_ref supports 1/2/4/8 byte types");

 public:
  using value_type = T;

  static constexpr std::size_t required_alignment = alignof(std::atomic<T>);
  static constexpr bool is_always_lock_free = std::atomic<T>::is_always_lock_free;

  explicit atomic_ref(T& obj) noexcept : ptr_(&obj) {
    METL_ASSERT((reinterpret_cast<std::uintptr_t>(ptr_) % required_alignment) == 0u);
  }

  atomic_ref(const atomic_ref&) noexcept = default;
  atomic_ref& operator=(const atomic_ref&) = delete;

  T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->load(order);
  }

  void store(T desired, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    detail::atomic_ref_cast(ptr_)->store(desired, order);
  }

  T exchange(T desired, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->exchange(desired, order);
  }

  bool compare_exchange_strong(T& expected,
                               T desired,
                               std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->compare_exchange_strong(expected, desired, order);
  }

  bool compare_exchange_weak(T& expected,
                             T desired,
                             std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->compare_exchange_weak(expected, desired, order);
  }

  // Integral-only fetch operations. SFINAE on a defaulted dependent
  // template parameter so the method only participates in overload
  // resolution when T is integral (and non-bool).
  template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>>>
  T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->fetch_add(arg, order);
  }

  template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>>>
  T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->fetch_sub(arg, order);
  }

  template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>>>
  T fetch_and(T arg, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->fetch_and(arg, order);
  }

  template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>>>
  T fetch_or(T arg, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->fetch_or(arg, order);
  }

  template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>>>
  T fetch_xor(T arg, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return detail::atomic_ref_cast(ptr_)->fetch_xor(arg, order);
  }

 private:
  T* ptr_;
};

}  // namespace metl
