#pragma once

// The iterator of flat_map and flat_set: a pointer in a class.
//
// A raw pointer would let `s.find(k) != s.end()` compile against a find that
// returns nullptr, and answer "present" for every key (#641). This class does
// not convert to or from a pointer: only the owner constructs one from a
// pointer, and none compares with nullptr or tests as a bool. So code written
// for a pointer-returning find fails to compile rather than run.
//
// Owner makes the iterators of two container types distinct, so one cannot be
// passed to the other's erase. On Cortex-M it is the same size as the pointer,
// and the code is no larger (local ARM GCC 16.2, -Os, #162).

#include "metl/compiler.hpp"
#include "metl/detail/abi.hpp"

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {

template <typename T, typename Owner>
class contiguous_iterator {
 public:
  using value_type = std::remove_cv_t<T>;
  using difference_type = std::ptrdiff_t;
  using pointer = T*;
  using reference = T&;
#if __cplusplus >= 202002L
  using iterator_concept = std::contiguous_iterator_tag;
#endif
  using iterator_category = std::random_access_iterator_tag;

  constexpr contiguous_iterator() noexcept = default;

  // iterator -> const_iterator.
  template <typename U, typename = std::enable_if_t<std::is_same_v<const U, T> && !std::is_same_v<U, T>>>
  constexpr contiguous_iterator(const contiguous_iterator<U, Owner>& other) noexcept  // NOLINT
      : p_(other.operator->()) {}

  constexpr reference operator*() const noexcept { return *p_; }
  constexpr pointer operator->() const noexcept { return p_; }
  METL_NODISCARD constexpr reference operator[](difference_type n) const noexcept { return p_[n]; }

  constexpr contiguous_iterator& operator++() noexcept {
    ++p_;
    return *this;
  }
  constexpr contiguous_iterator operator++(int) noexcept {
    contiguous_iterator c(*this);
    ++p_;
    return c;
  }
  constexpr contiguous_iterator& operator--() noexcept {
    --p_;
    return *this;
  }
  constexpr contiguous_iterator operator--(int) noexcept {
    contiguous_iterator c(*this);
    --p_;
    return c;
  }
  constexpr contiguous_iterator& operator+=(difference_type n) noexcept {
    p_ += n;
    return *this;
  }
  constexpr contiguous_iterator& operator-=(difference_type n) noexcept {
    p_ -= n;
    return *this;
  }

  friend constexpr contiguous_iterator operator+(contiguous_iterator it, difference_type n) noexcept {
    return it += n;
  }
  friend constexpr contiguous_iterator operator+(difference_type n, contiguous_iterator it) noexcept {
    return it += n;
  }
  friend constexpr contiguous_iterator operator-(contiguous_iterator it, difference_type n) noexcept {
    return it -= n;
  }
  friend constexpr difference_type operator-(contiguous_iterator a, contiguous_iterator b) noexcept {
    return a.p_ - b.p_;
  }

  friend constexpr bool operator==(contiguous_iterator a, contiguous_iterator b) noexcept {
    return a.p_ == b.p_;
  }
  friend constexpr bool operator!=(contiguous_iterator a, contiguous_iterator b) noexcept {
    return a.p_ != b.p_;
  }
  friend constexpr bool operator<(contiguous_iterator a, contiguous_iterator b) noexcept {
    return a.p_ < b.p_;
  }
  friend constexpr bool operator>(contiguous_iterator a, contiguous_iterator b) noexcept {
    return a.p_ > b.p_;
  }
  friend constexpr bool operator<=(contiguous_iterator a, contiguous_iterator b) noexcept {
    return a.p_ <= b.p_;
  }
  friend constexpr bool operator>=(contiguous_iterator a, contiguous_iterator b) noexcept {
    return a.p_ >= b.p_;
  }

 private:
  friend Owner;
  constexpr explicit contiguous_iterator(T* p) noexcept : p_(p) {}
  T* p_ = nullptr;
};

}  // namespace detail
METL_DETAIL_ABI_END
}  // namespace metl
