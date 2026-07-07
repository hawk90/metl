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

// ASan container annotations. The inline buffer is a single object, so the
// unused-capacity tail [size(), capacity()) is poisoned to catch out-of-bounds
// access past size() (à la absl::InlinedVector). Enabled only under
// AddressSanitizer; a no-op otherwise. Uses the manual poison interface.
#if defined(__SANITIZE_ADDRESS__) || METL_HAVE_FEATURE(address_sanitizer)
#define METL_FIXED_VECTOR_ASAN 1
#include <sanitizer/asan_interface.h>
#else
#define METL_FIXED_VECTOR_ASAN 0
#endif

namespace metl {

/// Contiguous sequence container with a compile-time FIXED capacity.
///
/// Stores up to `Capacity` elements inline; performs NO heap allocation.
/// Overflowing the capacity (via push_back/emplace_back/insert/resize/assign)
/// asserts and aborts by default; use the `try_*` variants for a non-asserting,
/// bool-returning path. Not thread-safe.
///
/// @tparam T Element type.
/// @tparam Capacity Maximum number of elements (fixed at compile time).
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

  /// Constructs an empty vector.
  constexpr fixed_vector() noexcept : size_(0) { asan_poison_tail_(); }

  /// Copy-constructs by copying each element of `other`.
  fixed_vector(const fixed_vector& other) : size_(0) {
    asan_poison_tail_();
    for (const auto& value : other) {
      emplace_back(value);
    }
  }

  /// Move-constructs by moving each element out of `other`, leaving it empty.
  fixed_vector(fixed_vector&& other) noexcept(std::is_nothrow_move_constructible<T>::value) : size_(0) {
    asan_poison_tail_();
    for (auto& value : other) {
      emplace_back(static_cast<T&&>(value));
    }
    other.clear();
  }

  /// Constructs from an initializer list.
  /// @pre `il.size() <= Capacity` (asserts otherwise).
  fixed_vector(std::initializer_list<T> il) : size_(0) {
    asan_poison_tail_();
    METL_ASSERT(il.size() <= Capacity);
    for (const auto& value : il) {
      emplace_back(value);
    }
  }

  ~fixed_vector() {
    clear();
    // Unpoison the whole buffer before the storage dies so no stale poison
    // outlives it (which would false-positive when the memory is reused).
    asan_unpoison_all_();
  }

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

  /// Returns an iterator to the first element.
  METL_NODISCARD constexpr iterator begin() noexcept { return data(); }
  METL_NODISCARD constexpr const_iterator begin() const noexcept { return data(); }
  METL_NODISCARD constexpr const_iterator cbegin() const noexcept { return data(); }

  /// Returns an iterator one past the last element.
  METL_NODISCARD constexpr iterator end() noexcept { return data() + size_; }
  METL_NODISCARD constexpr const_iterator end() const noexcept { return data() + size_; }
  METL_NODISCARD constexpr const_iterator cend() const noexcept { return data() + size_; }

  /// Returns a reverse iterator to the last element.
  METL_NODISCARD reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  METL_NODISCARD const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  METL_NODISCARD const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }

  /// Returns a reverse iterator one before the first element.
  METL_NODISCARD reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  METL_NODISCARD const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  METL_NODISCARD const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

  /// Returns a pointer to the underlying contiguous element storage.
  METL_NODISCARD pointer data() noexcept {
    return std::launder(reinterpret_cast<pointer>(storage_[0].addr()));
  }
  METL_NODISCARD const_pointer data() const noexcept {
    return std::launder(reinterpret_cast<const_pointer>(storage_[0].addr()));
  }

  /// Returns true if the vector holds no elements.
  METL_NODISCARD constexpr bool empty() const noexcept { return size_ == 0; }
  /// Returns true if the vector has reached its fixed capacity.
  METL_NODISCARD constexpr bool full() const noexcept { return size_ == Capacity; }
  /// Returns the number of elements currently stored.
  METL_NODISCARD constexpr size_type size() const noexcept { return size_; }
  /// Returns the fixed capacity (`Capacity`).
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  /// Returns the fixed capacity (`Capacity`); always equal to capacity().
  METL_NODISCARD constexpr size_type max_size() const noexcept { return Capacity; }
  /// Returns the number of bytes occupied by the current elements.
  METL_NODISCARD constexpr size_type size_bytes() const noexcept { return size_ * sizeof(T); }

  /// Accesses the element at `index` without bounds-checking against std logic.
  /// @pre `index < size()`; out-of-range asserts and aborts (does not throw).
  METL_NODISCARD reference operator[](size_type index) noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  /// Accesses the element at `index`.
  /// @pre `index < size()`; out-of-range asserts and aborts (does not throw).
  METL_NODISCARD const_reference operator[](size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  /// Accesses the element at `index`. Unlike std::vector::at, does NOT throw
  /// std::out_of_range: an out-of-range index asserts and aborts by default.
  /// @pre `index < size()`.
  METL_NODISCARD reference at(size_type index) noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  /// Accesses the element at `index`. Unlike std::vector::at, does NOT throw
  /// std::out_of_range: an out-of-range index asserts and aborts by default.
  /// @pre `index < size()`.
  METL_NODISCARD const_reference at(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }

  /// Returns a reference to the first element.
  /// @pre Container is non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference front() noexcept {
    METL_ASSERT(size_ > 0);
    return data()[0];
  }

  /// Returns a reference to the first element.
  /// @pre Container is non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference front() const noexcept {
    METL_ASSERT(size_ > 0);
    return data()[0];
  }

  /// Returns a reference to the last element.
  /// @pre Container is non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference back() noexcept {
    METL_ASSERT(size_ > 0);
    return data()[size_ - 1];
  }

  /// Returns a reference to the last element.
  /// @pre Container is non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference back() const noexcept {
    METL_ASSERT(size_ > 0);
    return data()[size_ - 1];
  }

  /// Constructs an element in place at the back if there is room.
  /// @return true on success; false if the container is full (no assert).
  template <typename... Args>
  bool try_emplace_back(Args&&... args) {
    if (full()) {
      return false;
    }

    asan_unpoison_all_();
    ::new (static_cast<void*>(slot_(size_))) T(std::forward<Args>(args)...);
    ++size_;
    asan_poison_tail_();
    return true;
  }

  /// Constructs an element in place at the back and returns a reference to it.
  /// @pre Container is not full; overflow asserts and aborts. Use
  /// try_emplace_back for a non-asserting path.
  template <typename... Args>
  reference emplace_back(Args&&... args) {
    const bool inserted = try_emplace_back(std::forward<Args>(args)...);
    METL_ASSERT(inserted);
    (void)inserted;
    return back();
  }

  /// Appends a copy of `value` if there is room; returns false when full.
  bool try_push_back(const T& value) { return try_emplace_back(value); }
  /// Appends `value` by move if there is room; returns false when full.
  bool try_push_back(T&& value) { return try_emplace_back(static_cast<T&&>(value)); }

  /// Appends a copy of `value`.
  /// @pre Container is not full; overflow asserts and aborts.
  reference push_back(const T& value) { return emplace_back(value); }
  /// Appends `value` by move.
  /// @pre Container is not full; overflow asserts and aborts.
  reference push_back(T&& value) { return emplace_back(static_cast<T&&>(value)); }

  /// Removes the last element.
  /// @pre Container is non-empty; asserts and aborts otherwise.
  void pop_back() noexcept {
    METL_ASSERT(size_ > 0);
    asan_unpoison_all_();
    data()[size_ - 1].~T();
    --size_;
    asan_poison_tail_();
  }

  /// Destroys all elements, leaving size() == 0.
  void clear() noexcept {
    while (size_ > 0) {
      pop_back();
    }
  }

  /// Inserts a copy of `value` before `pos`.
  /// @pre `pos` in [begin(), end()] and container is not full; asserts otherwise.
  iterator insert(const_iterator pos, const T& value) { return emplace(pos, value); }

  /// Inserts `value` (by move) before `pos`.
  /// @pre `pos` in [begin(), end()] and container is not full; asserts otherwise.
  iterator insert(const_iterator pos, T&& value) { return emplace(pos, static_cast<T&&>(value)); }

  /// Constructs an element in place before `pos`, shifting later elements right.
  /// @return Iterator to the newly inserted element.
  /// @pre `pos` in [begin(), end()] and container is not full; asserts otherwise.
  template <typename... Args>
  iterator emplace(const_iterator pos, Args&&... args) {
    METL_ASSERT(pos >= begin() && pos <= end());
    METL_ASSERT(size_ < Capacity);
    const size_type index = static_cast<size_type>(pos - begin());
    if (index == size_) {
      emplace_back(std::forward<Args>(args)...);
      return begin() + index;
    }
    asan_unpoison_all_();
    // Construct new element at end via move of last, then shift right.
    ::new (static_cast<void*>(slot_(size_))) T(static_cast<T&&>(data()[size_ - 1]));
    ++size_;
    for (size_type i = size_ - 2; i > index; --i) {
      data()[i] = static_cast<T&&>(data()[i - 1]);
    }
    data()[index].~T();
    ::new (static_cast<void*>(slot_(index))) T(std::forward<Args>(args)...);
    asan_poison_tail_();
    return begin() + index;
  }

  /// Inserts `n` copies of `value` before `pos`.
  /// @pre `pos` in [begin(), end()] and `size() + n <= Capacity`; asserts otherwise.
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

  /// Inserts the elements in [first, last) before `pos`.
  /// @pre `pos` in [begin(), end()] and the range fits in the remaining capacity.
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

  /// Removes the element at `pos`, shifting later elements left.
  /// @return Iterator to the element after the erased one.
  /// @pre `pos` in [begin(), end()); asserts otherwise.
  iterator erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable<T>::value) {
    METL_ASSERT(pos >= begin() && pos < end());
    const size_type index = static_cast<size_type>(pos - begin());
    asan_unpoison_all_();
    for (size_type i = index; i + 1 < size_; ++i) {
      data()[i] = static_cast<T&&>(data()[i + 1]);
    }
    data()[size_ - 1].~T();
    --size_;
    asan_poison_tail_();
    return begin() + index;
  }

  /// Removes the elements in [first, last), shifting later elements left.
  /// @return Iterator to the element after the last erased one.
  /// @pre `begin() <= first <= last <= end()`; asserts otherwise.
  iterator erase(const_iterator first,
                 const_iterator last) noexcept(std::is_nothrow_move_assignable<T>::value) {
    METL_ASSERT(first >= begin() && last <= end() && first <= last);
    const size_type first_index = static_cast<size_type>(first - begin());
    const size_type last_index = static_cast<size_type>(last - begin());
    const size_type erase_count = last_index - first_index;
    if (erase_count == 0) {
      return begin() + first_index;
    }
    asan_unpoison_all_();
    for (size_type i = last_index; i < size_; ++i) {
      data()[i - erase_count] = static_cast<T&&>(data()[i]);
    }
    for (size_type i = 0; i < erase_count; ++i) {
      data()[size_ - 1 - i].~T();
    }
    size_ -= erase_count;
    asan_poison_tail_();
    return begin() + first_index;
  }

  /// Resizes to `n` elements, default-constructing or removing from the back.
  /// @pre `n <= Capacity`; asserts otherwise.
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

  /// Resizes to `n` elements, appending copies of `value` when growing.
  /// @pre `n <= Capacity`; asserts otherwise.
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

  /// Replaces the contents with `n` copies of `value`.
  /// @pre `n <= Capacity`; asserts otherwise.
  void assign(size_type n, const T& value) {
    METL_ASSERT(n <= Capacity);
    clear();
    for (size_type i = 0; i < n; ++i) {
      emplace_back(value);
    }
  }

  /// Replaces the contents with the elements in [first, last).
  /// @pre The range fits within `Capacity`; asserts otherwise.
  template <typename It, typename = std::enable_if_t<!std::is_integral<It>::value>>
  void assign(It first, It last) {
    clear();
    for (It it = first; it != last; ++it) {
      METL_ASSERT(size_ < Capacity);
      emplace_back(*it);
    }
  }

  /// Swaps contents with `other` element-wise (no pointer swap; capacity is fixed).
  void swap(fixed_vector& other) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                          std::is_nothrow_move_assignable<T>::value) {
    if (this == &other) {
      return;
    }
    // Both buffers are written past their current sizes below, so expose the
    // full capacity of each for the duration of the swap, then re-poison tails.
    asan_unpoison_all_();
    other.asan_unpoison_all_();
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
    asan_poison_tail_();
    other.asan_poison_tail_();
  }

  /// Returns a span viewing the current elements [0, size()).
  span<T> as_span() noexcept { return span<T>(data(), size_); }
  /// Returns a read-only span viewing the current elements [0, size()).
  span<const T> as_span() const noexcept { return span<const T>(data(), size_); }

 private:
  void* slot_(size_type index) noexcept { return storage_[index].addr(); }

#if METL_FIXED_VECTOR_ASAN
  // Poison the unused-capacity tail [size_, Capacity). Rounding in the ASan
  // interface never poisons into the live range [0, size_), so element access
  // and iteration never false-positive.
  void asan_poison_tail_() noexcept {
    if (size_ < Capacity) {
      ASAN_POISON_MEMORY_REGION(&storage_[size_], (Capacity - size_) * sizeof(storage_for<T>));
    }
  }
  // Expose the whole buffer while a mutating op rearranges elements internally.
  void asan_unpoison_all_() noexcept {
    if (Capacity != 0) {
      ASAN_UNPOISON_MEMORY_REGION(&storage_[0], Capacity * sizeof(storage_for<T>));
    }
  }
#else
  // No-ops. constexpr so the constexpr default constructor stays constexpr.
  constexpr void asan_poison_tail_() noexcept {}
  constexpr void asan_unpoison_all_() noexcept {}
#endif

  // NOTE: elements live in an aligned byte buffer accessed through
  // std::launder, which is not constant-evaluable, so the constexpr labels
  // here are effective only outside constant evaluation. Genuine constexpr
  // (as done for metl::optional via metl/detail/construct.hpp) would require a
  // union-of-T storage rewrite that also has to reconcile with the ASan
  // tail-poisoning above; deferred (see docs/AUDIT.md Section A).
  storage_for<T> storage_[Capacity == 0 ? 1 : Capacity];
  size_type size_;
};

/// Returns true if both vectors have the same size and equal elements.
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

/// Returns true if the vectors differ in size or any element.
template <typename T, std::size_t N1, std::size_t N2>
inline bool operator!=(const fixed_vector<T, N1>& lhs, const fixed_vector<T, N2>& rhs) {
  return !(lhs == rhs);
}

/// Lexicographically compares two vectors.
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

/// Lexicographically compares two vectors.
template <typename T, std::size_t N1, std::size_t N2>
inline bool operator>(const fixed_vector<T, N1>& lhs, const fixed_vector<T, N2>& rhs) {
  return rhs < lhs;
}

/// Lexicographically compares two vectors.
template <typename T, std::size_t N1, std::size_t N2>
inline bool operator<=(const fixed_vector<T, N1>& lhs, const fixed_vector<T, N2>& rhs) {
  return !(rhs < lhs);
}

/// Lexicographically compares two vectors.
template <typename T, std::size_t N1, std::size_t N2>
inline bool operator>=(const fixed_vector<T, N1>& lhs, const fixed_vector<T, N2>& rhs) {
  return !(lhs < rhs);
}

/// Swaps the contents of two vectors of the same type and capacity.
template <typename T, std::size_t N>
inline void swap(fixed_vector<T, N>& a, fixed_vector<T, N>& b) noexcept(noexcept(a.swap(b))) {
  a.swap(b);
}

/// Removes every element of `v` for which `pred` returns true.
/// @return The number of elements removed.
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
