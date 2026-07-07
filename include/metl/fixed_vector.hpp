#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/span.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

template <typename T, std::size_t Capacity>
class fixed_vector {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = T*;
  using const_iterator = const T*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  constexpr fixed_vector() noexcept : size_(0) {}

  fixed_vector(const fixed_vector& other) : size_(0) {
    for (const auto& value : other) {
      emplace_back(value);
    }
  }

  fixed_vector(fixed_vector&& other) noexcept(std::is_nothrow_move_constructible<T>::value) : size_(0) {
    for (auto& value : other) {
      emplace_back(static_cast<T&&>(value));
    }
    other.clear();
  }

  fixed_vector(std::initializer_list<T> il) : size_(0) {
    METL_ASSERT(il.size() <= Capacity);
    for (const auto& value : il) {
      emplace_back(value);
    }
  }

  ~fixed_vector() { clear(); }

  fixed_vector& operator=(const fixed_vector& other) {
    if (this == &other) {
      return *this;
    }

    clear();
    for (const auto& value : other) {
      emplace_back(value);
    }

    return *this;
  }

  fixed_vector& operator=(fixed_vector&& other) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                                         std::is_nothrow_move_assignable<T>::value) {
    if (this == &other) {
      return *this;
    }

    clear();
    for (auto& value : other) {
      emplace_back(static_cast<T&&>(value));
    }
    other.clear();

    return *this;
  }

  METL_NODISCARD constexpr iterator begin() noexcept { return data(); }
  METL_NODISCARD constexpr const_iterator begin() const noexcept { return data(); }
  METL_NODISCARD constexpr const_iterator cbegin() const noexcept { return data(); }

  METL_NODISCARD constexpr iterator end() noexcept { return data() + size_; }
  METL_NODISCARD constexpr const_iterator end() const noexcept { return data() + size_; }
  METL_NODISCARD constexpr const_iterator cend() const noexcept { return data() + size_; }

  METL_NODISCARD reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  METL_NODISCARD const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  METL_NODISCARD const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }

  METL_NODISCARD reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  METL_NODISCARD const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  METL_NODISCARD const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

  METL_NODISCARD pointer data() noexcept {
    return std::launder(reinterpret_cast<pointer>(storage_[0].addr()));
  }
  METL_NODISCARD const_pointer data() const noexcept {
    return std::launder(reinterpret_cast<const_pointer>(storage_[0].addr()));
  }

  METL_NODISCARD constexpr bool empty() const noexcept { return size_ == 0; }
  METL_NODISCARD constexpr bool full() const noexcept { return size_ == Capacity; }
  METL_NODISCARD constexpr size_type size() const noexcept { return size_; }
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  METL_NODISCARD constexpr size_type max_size() const noexcept { return Capacity; }
  METL_NODISCARD constexpr size_type size_bytes() const noexcept { return size_ * sizeof(T); }

  METL_NODISCARD reference operator[](size_type index) noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  METL_NODISCARD const_reference operator[](size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  METL_NODISCARD reference at(size_type index) noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  METL_NODISCARD const_reference at(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  METL_NODISCARD reference front() noexcept {
    METL_ASSERT(size_ > 0);
    return data()[0];
  }

  METL_NODISCARD const_reference front() const noexcept {
    METL_ASSERT(size_ > 0);
    return data()[0];
  }

  METL_NODISCARD reference back() noexcept {
    METL_ASSERT(size_ > 0);
    return data()[size_ - 1];
  }

  METL_NODISCARD const_reference back() const noexcept {
    METL_ASSERT(size_ > 0);
    return data()[size_ - 1];
  }

  template <typename... Args>
  bool try_emplace_back(Args&&... args) {
    if (full()) {
      return false;
    }

    ::new (static_cast<void*>(slot_(size_))) T(std::forward<Args>(args)...);
    ++size_;
    return true;
  }

  template <typename... Args>
  reference emplace_back(Args&&... args) {
    const bool inserted = try_emplace_back(std::forward<Args>(args)...);
    METL_ASSERT(inserted);
    (void)inserted;
    return back();
  }

  bool try_push_back(const T& value) { return try_emplace_back(value); }
  bool try_push_back(T&& value) { return try_emplace_back(static_cast<T&&>(value)); }

  reference push_back(const T& value) { return emplace_back(value); }
  reference push_back(T&& value) { return emplace_back(static_cast<T&&>(value)); }

  void pop_back() noexcept {
    METL_ASSERT(size_ > 0);
    data()[size_ - 1].~T();
    --size_;
  }

  void clear() noexcept {
    while (size_ > 0) {
      pop_back();
    }
  }

  // Insert/emplace at iterator position.
  iterator insert(const_iterator pos, const T& value) { return emplace(pos, value); }

  iterator insert(const_iterator pos, T&& value) { return emplace(pos, static_cast<T&&>(value)); }

  template <typename... Args>
  iterator emplace(const_iterator pos, Args&&... args) {
    METL_ASSERT(pos >= begin() && pos <= end());
    METL_ASSERT(size_ < Capacity);
    const size_type index = static_cast<size_type>(pos - begin());
    if (index == size_) {
      emplace_back(std::forward<Args>(args)...);
      return begin() + index;
    }
    // Construct new element at end via move of last, then shift right.
    ::new (static_cast<void*>(slot_(size_))) T(static_cast<T&&>(data()[size_ - 1]));
    ++size_;
    for (size_type i = size_ - 2; i > index; --i) {
      data()[i] = static_cast<T&&>(data()[i - 1]);
    }
    data()[index].~T();
    ::new (static_cast<void*>(slot_(index))) T(std::forward<Args>(args)...);
    return begin() + index;
  }

  iterator insert(const_iterator pos, size_type n, const T& value) {
    METL_ASSERT(pos >= begin() && pos <= end());
    METL_ASSERT(size_ + n <= Capacity);
    const size_type index = static_cast<size_type>(pos - begin());
    if (n == 0) {
      return begin() + index;
    }
    // Simple implementation: insert one-by-one via emplace.
    // `value` is a const T& and emplace makes copies, so reference stability is fine.
    for (size_type i = 0; i < n; ++i) {
      emplace(begin() + index + i, value);
    }
    return begin() + index;
  }

  template <typename It, typename = std::enable_if_t<!std::is_integral<It>::value>>
  iterator insert(const_iterator pos, It first, It last) {
    METL_ASSERT(pos >= begin() && pos <= end());
    const size_type index = static_cast<size_type>(pos - begin());
    iterator out = begin() + index;
    // Generic forward-iterator path: insert one-by-one.
    size_type offset = 0;
    for (It it = first; it != last; ++it) {
      emplace(begin() + index + offset, *it);
      ++offset;
    }
    return out;
  }

  // Erase.
  iterator erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable<T>::value) {
    METL_ASSERT(pos >= begin() && pos < end());
    const size_type index = static_cast<size_type>(pos - begin());
    for (size_type i = index; i + 1 < size_; ++i) {
      data()[i] = static_cast<T&&>(data()[i + 1]);
    }
    data()[size_ - 1].~T();
    --size_;
    return begin() + index;
  }

  iterator erase(const_iterator first,
                 const_iterator last) noexcept(std::is_nothrow_move_assignable<T>::value) {
    METL_ASSERT(first >= begin() && last <= end() && first <= last);
    const size_type first_index = static_cast<size_type>(first - begin());
    const size_type last_index = static_cast<size_type>(last - begin());
    const size_type erase_count = last_index - first_index;
    if (erase_count == 0) {
      return begin() + first_index;
    }
    for (size_type i = last_index; i < size_; ++i) {
      data()[i - erase_count] = static_cast<T&&>(data()[i]);
    }
    for (size_type i = 0; i < erase_count; ++i) {
      data()[size_ - 1 - i].~T();
    }
    size_ -= erase_count;
    return begin() + first_index;
  }

  // Resize.
  void resize(size_type n) {
    METL_ASSERT(n <= Capacity);
    if (n < size_) {
      while (size_ > n) {
        pop_back();
      }
    } else {
      while (size_ < n) {
        emplace_back();
      }
    }
  }

  void resize(size_type n, const T& value) {
    METL_ASSERT(n <= Capacity);
    if (n < size_) {
      while (size_ > n) {
        pop_back();
      }
    } else {
      while (size_ < n) {
        emplace_back(value);
      }
    }
  }

  // Assign.
  void assign(size_type n, const T& value) {
    METL_ASSERT(n <= Capacity);
    clear();
    for (size_type i = 0; i < n; ++i) {
      emplace_back(value);
    }
  }

  template <typename It, typename = std::enable_if_t<!std::is_integral<It>::value>>
  void assign(It first, It last) {
    clear();
    for (It it = first; it != last; ++it) {
      METL_ASSERT(size_ < Capacity);
      emplace_back(*it);
    }
  }

  // Swap (element-wise).
  void swap(fixed_vector& other) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                          std::is_nothrow_move_assignable<T>::value) {
    if (this == &other) {
      return;
    }
    const size_type common = (size_ < other.size_) ? size_ : other.size_;
    using std::swap;
    for (size_type i = 0; i < common; ++i) {
      swap(data()[i], other.data()[i]);
    }
    if (size_ < other.size_) {
      // Move-construct tail from `other` into `this`, then shrink `other`.
      for (size_type i = size_; i < other.size_; ++i) {
        ::new (static_cast<void*>(slot_(i))) T(static_cast<T&&>(other.data()[i]));
      }
      for (size_type i = other.size_; i > size_; --i) {
        other.data()[i - 1].~T();
      }
      const size_type new_self = other.size_;
      const size_type new_other = size_;
      size_ = new_self;
      other.size_ = new_other;
    } else if (size_ > other.size_) {
      for (size_type i = other.size_; i < size_; ++i) {
        ::new (static_cast<void*>(other.slot_(i))) T(static_cast<T&&>(data()[i]));
      }
      for (size_type i = size_; i > other.size_; --i) {
        data()[i - 1].~T();
      }
      const size_type new_self = other.size_;
      const size_type new_other = size_;
      size_ = new_self;
      other.size_ = new_other;
    }
  }

  span<T> as_span() noexcept { return span<T>(data(), size_); }
  span<const T> as_span() const noexcept { return span<const T>(data(), size_); }

 private:
  void* slot_(size_type index) noexcept { return storage_[index].addr(); }

  storage_for<T> storage_[Capacity == 0 ? 1 : Capacity];
  size_type size_;
};

// Comparison operators (free functions).
template <typename T, std::size_t N1, std::size_t N2>
inline bool operator==(const fixed_vector<T, N1>& lhs, const fixed_vector<T, N2>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!(lhs[i] == rhs[i])) {
      return false;
    }
  }
  return true;
}

template <typename T, std::size_t N1, std::size_t N2>
inline bool operator!=(const fixed_vector<T, N1>& lhs, const fixed_vector<T, N2>& rhs) {
  return !(lhs == rhs);
}

template <typename T, std::size_t N1, std::size_t N2>
inline bool operator<(const fixed_vector<T, N1>& lhs, const fixed_vector<T, N2>& rhs) {
  const std::size_t n = (lhs.size() < rhs.size()) ? lhs.size() : rhs.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (lhs[i] < rhs[i]) {
      return true;
    }
    if (rhs[i] < lhs[i]) {
      return false;
    }
  }
  return lhs.size() < rhs.size();
}

template <typename T, std::size_t N1, std::size_t N2>
inline bool operator>(const fixed_vector<T, N1>& lhs, const fixed_vector<T, N2>& rhs) {
  return rhs < lhs;
}

template <typename T, std::size_t N1, std::size_t N2>
inline bool operator<=(const fixed_vector<T, N1>& lhs, const fixed_vector<T, N2>& rhs) {
  return !(rhs < lhs);
}

template <typename T, std::size_t N1, std::size_t N2>
inline bool operator>=(const fixed_vector<T, N1>& lhs, const fixed_vector<T, N2>& rhs) {
  return !(lhs < rhs);
}

// Free swap.
template <typename T, std::size_t N>
inline void swap(fixed_vector<T, N>& a, fixed_vector<T, N>& b) noexcept(noexcept(a.swap(b))) {
  a.swap(b);
}

// erase_if free function. Returns number of elements removed.
template <typename T, std::size_t N, typename Pred>
typename fixed_vector<T, N>::size_type erase_if(fixed_vector<T, N>& v, Pred pred) {
  using size_type = typename fixed_vector<T, N>::size_type;
  size_type write = 0;
  const size_type old_size = v.size();
  for (size_type read = 0; read < old_size; ++read) {
    if (!pred(v[read])) {
      if (write != read) {
        v[write] = static_cast<T&&>(v[read]);
      }
      ++write;
    }
  }
  const size_type removed = old_size - write;
  while (v.size() > write) {
    v.pop_back();
  }
  return removed;
}

}  // namespace metl
