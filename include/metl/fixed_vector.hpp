#pragma once

/// @file
/// @brief Progress guarantees for `metl::fixed_vector` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `push_back`, `pop_back`, `emplace_back`, `try_push_back`, `try_emplace_back`, `back`, indexing |
///   wait-free, bounded | | `insert`/`emplace`/`try_insert`/`try_emplace` of one element, `erase` |
///   wait-free, bounded by `size()` moves | | `insert(pos, n, v)`, range `insert`, and their `try_insert`
///   forms | wait-free, bounded by `n * size()` moves | | `clear`, copy, destructor | wait-free, bounded by
///   `size()` | | `resize`, `try_resize`, `assign`, `try_assign` | wait-free, bounded by `size()` + `n` |
///
/// Appending and removing at the end are one construction or destruction plus an
/// index update; they do not depend on `size()`. Inserting or erasing in the middle
/// shifts the tail, so one element costs `size()` moves of `T` -- at most `Capacity`;
/// inserting `n` elements shifts the tail `n` times.
///
/// This is `O(n)` with a compile-time-known `n`, which docs/SCOPE.md section 1
/// accepts and distinguishes from `O(1) amortized` with a reallocation cliff. There
/// is no reallocation here: the capacity is fixed and an overflowing `push_back`
/// asserts, while `try_push_back` returns false.
///
/// Single-threaded: this type does not synchronise.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/array_storage.hpp"
#include "metl/scope_exit.hpp"
#include "metl/span.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <cstdint>
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
namespace detail {
template <typename It>
struct is_reverse_iterator_over_pointer : std::false_type {};
template <typename P>
struct is_reverse_iterator_over_pointer<std::reverse_iterator<P>> : std::is_pointer<P> {};
template <typename It>
inline constexpr bool is_reverse_iterator_over_pointer_v = is_reverse_iterator_over_pointer<It>::value;
}  // namespace detail

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
  static_assert(std::is_object_v<T> && !std::is_array_v<T>, METL_DETAIL_OBJECT_TYPE_MESSAGE);

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

  // Element-inserting constructors delegate to the empty constructor first.
  // Once it returns the object is fully constructed, so if copying or moving
  // an element throws part-way, the destructor runs and destroys exactly the
  // elements already inserted; nothing leaks.
  /// Copy-constructs by copying each element of `other`.
  fixed_vector(const fixed_vector& other) : fixed_vector() {
    for (const auto& value : other) {
      emplace_back(value);
    }
  }

  /// Move-constructs by moving each element out of `other`, leaving it empty.
  fixed_vector(fixed_vector&& other) noexcept(std::is_nothrow_move_constructible_v<T>) : fixed_vector() {
    for (auto& value : other) {
      emplace_back(static_cast<T&&>(value));
    }
    other.clear();
  }

  /// Constructs from an initializer list.
  /// @pre `il.size() <= Capacity` (asserts otherwise).
  fixed_vector(std::initializer_list<T> il) : fixed_vector() {
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

  fixed_vector& operator=(fixed_vector&& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                         std::is_nothrow_move_assignable_v<T>) {
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
  /// @note A pointer into one `T[Capacity]` array object, so `data() + i` is
  ///       well-defined for every `i <= Capacity` (see detail/array_storage.hpp).
  METL_NODISCARD pointer data() noexcept { return storage_.data(); }
  METL_NODISCARD const_pointer data() const noexcept { return storage_.data(); }

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

  /// Accesses the element at `index`.
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
  METL_NODISCARD bool try_emplace_back(Args&&... args) {
    if (full()) {
      return false;
    }

    asan_unpoison_all_();
    // Claim the slot before T's constructor runs: a constructor that pushes
    // into this vector then gets the next slot instead of this one. Undone if
    // the constructor throws.
    const size_type index = size_++;
    auto undo = make_scope_exit([this]() noexcept {
      --size_;
      asan_poison_tail_();
    });
    ::new (static_cast<void*>(slot_(index))) T(std::forward<Args>(args)...);
    undo.release();
    asan_poison_tail_();
    return true;
  }

  /// Constructs an element in place at the back and returns a reference to it.
  /// @pre Container is not full; overflow asserts and aborts. Use
  /// try_emplace_back for a non-asserting path.
  template <typename... Args>
  reference emplace_back(Args&&... args) {
    // Where the element will be built, taken before T's constructor runs: if
    // that constructor pushes into this vector, back() is the inner element.
    const size_type index = size_;
    const bool inserted = try_emplace_back(std::forward<Args>(args)...);
    METL_ASSERT(inserted);
    if (!inserted) {
      // With METL_ASSERT stripped, a full vector returns its last element -- in
      // bounds, unless Capacity is 0 and back() would be data()[SIZE_MAX].
      METL_HARDEN(size_ > 0);
      return back();
    }
    return data()[index];
  }

  /// Appends a copy of `value` if there is room; returns false when full.
  METL_NODISCARD bool try_push_back(const T& value) { return try_emplace_back(value); }
  /// Appends `value` by move if there is room; returns false when full.
  METL_NODISCARD bool try_push_back(T&& value) { return try_emplace_back(static_cast<T&&>(value)); }

  /// Appends a copy of `value`.
  /// @pre Container is not full; overflow asserts and aborts.
  reference push_back(const T& value) { return emplace_back(value); }
  /// Appends `value` by move.
  /// @pre Container is not full; overflow asserts and aborts.
  reference push_back(T&& value) { return emplace_back(static_cast<T&&>(value)); }

  /// Removes the last element.
  /// @note The element leaves the container before its destructor runs, so a
  ///       destructor that calls back to remove elements sees it gone. It must
  ///       not insert: the slot being destroyed may be the next one handed out.
  /// @pre Container is non-empty; asserts and aborts otherwise.
  void pop_back() noexcept {
    // Hard: an empty pop would destroy data()[-1] and wrap size_ to SIZE_MAX,
    // after which the next push writes far out of bounds.
    METL_HARDEN(size_ > 0);
    asan_unpoison_all_();
    // Shrink first, then destroy: a destructor that calls back into this
    // vector (a handle that unregisters itself) must see the element gone,
    // not destroy it a second time.
    --size_;
    data()[size_].~T();
    asan_poison_tail_();
  }

  /// Destroys all elements, leaving size() == 0.
  void clear() noexcept {
    while (size_ > 0) {
      pop_back();
    }
  }

  /// Inserts a copy of `value` before `pos` if there is room.
  /// @return Iterator to the new element, or `end()` if the container is full
  ///         (contents unchanged).
  /// @pre `pos` in [begin(), end()]; a bad iterator is still a programming error
  ///      and asserts. Only the capacity failure is recoverable.
  METL_NODISCARD iterator try_insert(const_iterator pos, const T& value) { return try_emplace(pos, value); }

  /// Inserts `value` (by move) before `pos` if there is room.
  /// @return Iterator to the new element, or `end()` if the container is full
  ///         (contents unchanged; `value` is not moved from).
  /// @note Spelled `std::move` rather than this file's usual `static_cast<T&&>`
  ///       (which is the same thing) because clang-tidy's
  ///       `cppcoreguidelines-rvalue-reference-param-not-moved` only recognises the
  ///       named form, and a new entry in the analysis budget for a cast style is
  ///       not worth spending. `spsc_queue`/`mpmc_queue` already spell it this way.
  METL_NODISCARD iterator try_insert(const_iterator pos, T&& value) {
    return try_emplace(pos, std::move(value));
  }

  /// Constructs an element in place before `pos` if there is room.
  /// @return Iterator to the new element, or `end()` if the container is full
  ///         (contents unchanged).
  /// @note `end()` is unambiguous as the failure marker: a successful insert always
  ///       yields an iterator to a live element, which `end()` never is.
  template <typename... Args>
  METL_NODISCARD iterator try_emplace(const_iterator pos, Args&&... args) {
    METL_HARDEN(pos >= begin() && pos <= end());
    if (size_ == Capacity) {
      return end();
    }
    return emplace(pos, std::forward<Args>(args)...);
  }

  /// Inserts `n` copies of `value` before `pos` if they all fit.
  /// @return Iterator to the first new element, or `end()` if they do not fit
  ///         (contents unchanged — this is all-or-nothing, never partial).
  METL_NODISCARD iterator try_insert(const_iterator pos, size_type n, const T& value) {
    METL_HARDEN(pos >= begin() && pos <= end());
    if (n > Capacity - size_) {
      return end();
    }
    return insert(pos, n, value);
  }

  /// Inserts the elements in [first, last) before `pos` if they all fit.
  /// @return Iterator to the first new element, or `end()` if they do not fit
  ///         (contents unchanged).
  /// @note Forward iterators only. "Contents unchanged on failure" requires knowing
  ///       the length before writing anything, and a single-pass input iterator
  ///       cannot be measured without consuming it. Use the asserting `insert`, or
  ///       stage into a `fixed_vector` first.
  template <typename It, typename = std::enable_if_t<!std::is_integral_v<It>>>
  METL_NODISCARD iterator try_insert(const_iterator pos, It first, It last) {
    static_assert(
        std::is_base_of_v<std::forward_iterator_tag, typename std::iterator_traits<It>::iterator_category>,
        "try_insert requires a forward iterator: the range must be measurable "
        "before anything is written, so that a failure leaves contents unchanged");
    METL_HARDEN(pos >= begin() && pos <= end());
    if (static_cast<size_type>(std::distance(first, last)) > Capacity - size_) {
      return end();
    }
    return insert(pos, first, last);
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
    METL_HARDEN(pos >= begin() && pos <= end());
    // METL_HARDEN, not METL_ASSERT: on a full vector the shift below writes one
    // past the storage, so stripping this check turns a precondition violation
    // into memory corruption.
    METL_HARDEN(size_ < Capacity);
    const size_type index = static_cast<size_type>(pos - begin());
    if (index == size_) {
      emplace_back(std::forward<Args>(args)...);
      return begin() + index;
    }
    // Build the new value before shifting: `args` may refer to an element of
    // this vector (`v.insert(v.begin(), v[0])`), which the shift moves from.
    T value(std::forward<Args>(args)...);
    asan_unpoison_all_();
    // Construct new element at end via move of last, then shift right.
    ::new (static_cast<void*>(slot_(size_))) T(static_cast<T&&>(data()[size_ - 1]));
    ++size_;
    for (size_type i = size_ - 2; i > index; --i) {
      data()[i] = static_cast<T&&>(data()[i - 1]);
    }
    data()[index] = static_cast<T&&>(value);
    asan_poison_tail_();
    return begin() + index;
  }

  /// Inserts `n` copies of `value` before `pos`.
  /// @pre `pos` in [begin(), end()] and `size() + n <= Capacity`; asserts otherwise.
  iterator insert(const_iterator pos, size_type n, const T& value) {
    METL_HARDEN(pos >= begin() && pos <= end());
    // Written as a subtraction, not `size_ + n <= Capacity`: the sum overflows for
    // a large `n` and would wrap into a passing assert.
    METL_ASSERT(n <= Capacity - size_);
    const size_type index = static_cast<size_type>(pos - begin());
    if (n == 0) {
      return begin() + index;
    }
    // Copy once up front: `value` may refer to an element of this vector, and
    // each emplace below shifts it, so re-reading it would copy a neighbour.
    const T copy(value);
    for (size_type i = 0; i < n; ++i) {
      emplace(begin() + index + i, copy);
    }
    return begin() + index;
  }

  /// Inserts the elements in [first, last) before `pos`.
  /// @pre `pos` in [begin(), end()] and the range fits in the remaining capacity.
  /// @pre [first, last) is not a range of this vector -- it is shifted or
  ///      cleared before it is read (std::vector has the same precondition).
  ///      Checked: a range of this vector's own pointers or reverse iterators
  ///      asserts instead of silently copying moved-from or destroyed elements.
  ///      Copy it out first.
  template <typename It, typename = std::enable_if_t<!std::is_integral_v<It>>>
  iterator insert(const_iterator pos, It first, It last) {
    METL_HARDEN(pos >= begin() && pos <= end());
    METL_ASSERT(!aliases_own_storage(first, last));
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
  iterator erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
    METL_HARDEN(pos >= begin() && pos < end());
    const size_type index = static_cast<size_type>(pos - begin());
    asan_unpoison_all_();
    for (size_type i = index; i + 1 < size_; ++i) {
      data()[i] = static_cast<T&&>(data()[i + 1]);
    }
    --size_;  // shrink first, as in pop_back
    data()[size_].~T();
    asan_poison_tail_();
    return begin() + index;
  }

  /// Removes the elements in [first, last), shifting later elements left.
  /// @return Iterator to the element after the last erased one.
  /// @pre `begin() <= first <= last <= end()`; asserts otherwise.
  iterator erase(const_iterator first, const_iterator last) noexcept(std::is_nothrow_move_assignable_v<T>) {
    METL_HARDEN(first >= begin() && last <= end() && first <= last);
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
    const size_type old_size = size_;
    size_ -= erase_count;  // shrink first, as in pop_back
    for (size_type i = size_; i < old_size; ++i) {
      data()[i].~T();
    }
    asan_poison_tail_();
    return begin() + first_index;
  }

  /// Resizes to `n` elements if `n` fits, default-constructing or removing from the back.
  /// @return true on success; false if `n > capacity()` (contents unchanged).
  METL_NODISCARD bool try_resize(size_type n) {
    if (n > Capacity) {
      return false;
    }
    resize(n);
    return true;
  }

  /// Resizes to `n` elements if `n` fits, appending copies of `value` when growing.
  /// @return true on success; false if `n > capacity()` (contents unchanged).
  METL_NODISCARD bool try_resize(size_type n, const T& value) {
    if (n > Capacity) {
      return false;
    }
    resize(n, value);
    return true;
  }

  /// Replaces the contents with `n` copies of `value` if they fit.
  /// @return true on success; false if `n > capacity()` (contents unchanged — the
  ///         check happens before the existing elements are destroyed).
  METL_NODISCARD bool try_assign(size_type n, const T& value) {
    if (n > Capacity) {
      return false;
    }
    assign(n, value);
    return true;
  }

  /// Replaces the contents with the elements in [first, last) if they fit.
  /// @return true on success; false if the range does not fit (contents unchanged).
  /// @note Forward iterators only, for the reason given on `try_insert`.
  template <typename It, typename = std::enable_if_t<!std::is_integral_v<It>>>
  METL_NODISCARD bool try_assign(It first, It last) {
    static_assert(
        std::is_base_of_v<std::forward_iterator_tag, typename std::iterator_traits<It>::iterator_category>,
        "try_assign requires a forward iterator: the range must be measurable "
        "before anything is written, so that a failure leaves contents unchanged");
    if (static_cast<size_type>(std::distance(first, last)) > Capacity) {
      return false;
    }
    assign(first, last);
    return true;
  }

  /// Resizes to `n` elements, default-constructing or removing from the back.
  /// @pre `n <= Capacity`; asserts otherwise.
  void resize(size_type n) {
    METL_HARDEN(n <= Capacity);
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
    METL_HARDEN(n <= Capacity);
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
    // Copy first: `value` may be an element of this vector (`v.assign(3, v[0])`),
    // which clear() destroys -- every copy then read a dead object.
    const T copy(value);
    clear();
    for (size_type i = 0; i < n; ++i) {
      emplace_back(copy);
    }
  }

  /// Replaces the contents with the elements in [first, last).
  /// @pre The range fits within `Capacity`; asserts otherwise.
  /// @pre [first, last) is not a range of this vector -- it is shifted or
  ///      cleared before it is read (std::vector has the same precondition).
  ///      Checked: a range of this vector's own pointers or reverse iterators
  ///      asserts instead of silently copying moved-from or destroyed elements.
  ///      Copy it out first.
  template <typename It, typename = std::enable_if_t<!std::is_integral_v<It>>>
  void assign(It first, It last) {
    METL_HARDEN(!aliases_own_storage(first, last));
    clear();
    for (It it = first; it != last; ++it) {
      METL_ASSERT(size_ < Capacity);
      emplace_back(*it);
    }
  }

  /// Swaps contents with `other` element-wise (no pointer swap; capacity is fixed).
  void swap(fixed_vector& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                          std::is_nothrow_move_assignable_v<T>) {
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
    if (size_ != other.size_) {
      fixed_vector& shorter = (size_ < other.size_) ? *this : other;
      fixed_vector& longer = (size_ < other.size_) ? other : *this;
      const size_type old_shorter = shorter.size_;
      // Move the tail across one element at a time, counting each into
      // `shorter.size_` as soon as it exists. A throwing move then leaves each
      // vector owning exactly the objects it holds; counting only after the
      // loop leaked every element moved before the throw.
      for (size_type i = old_shorter; i < longer.size_; ++i) {
        ::new (static_cast<void*>(shorter.slot_(i))) T(static_cast<T&&>(longer.data()[i]));
        ++shorter.size_;
      }
      for (size_type i = longer.size_; i > old_shorter; --i) {
        longer.data()[i - 1].~T();
      }
      longer.size_ = old_shorter;
    }
    asan_poison_tail_();
    other.asan_poison_tail_();
  }

  /// Returns a span viewing the current elements [0, size()).
  span<T> as_span() noexcept { return span<T>(data(), size_); }
  /// Returns a read-only span viewing the current elements [0, size()).
  span<const T> as_span() const noexcept { return span<const T>(data(), size_); }

 private:
  void* slot_(size_type index) noexcept { return storage_.slot(index); }

  // Whether [first, last) starts inside this vector's storage -- the self-range
  // that insert and assign cannot read correctly. Only pointer iterators (this
  // vector's own iterator type) and reverse iterators over them can point here;
  // any other iterator type cannot, and answers false. Compared as integers,
  // like object_pool::index_of, because relational `<` on pointers that may not
  // share an array is unspecified.
  template <typename It>
  bool aliases_own_storage(It first, It last) const noexcept {
    if constexpr (std::is_pointer_v<It> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<It>>, T>) {
      if (first == last) {
        return false;
      }
      const auto begin_address = reinterpret_cast<std::uintptr_t>(data());
      const auto end_address = reinterpret_cast<std::uintptr_t>(data() + Capacity);
      const auto first_address = reinterpret_cast<std::uintptr_t>(first);
      return first_address >= begin_address && first_address < end_address;
    } else if constexpr (detail::is_reverse_iterator_over_pointer_v<It>) {
      return aliases_own_storage(last.base(), first.base());
    } else {
      (void)first;
      (void)last;
      return false;
    }
  }

#if METL_FIXED_VECTOR_ASAN
  // Poison the unused-capacity tail [size_, Capacity). Rounding in the ASan
  // interface never poisons into the live range [0, size_), so element access
  // and iteration never false-positive.
  void asan_poison_tail_() noexcept {
    if (size_ < Capacity) {
      ASAN_POISON_MEMORY_REGION(storage_.slot(size_), (Capacity - size_) * sizeof(T));
    }
  }
  // Expose the whole buffer while a mutating op rearranges elements internally.
  void asan_unpoison_all_() noexcept {
    if (Capacity != 0) {
      ASAN_UNPOISON_MEMORY_REGION(storage_.slot(0), Capacity * sizeof(T));
    }
  }
#else
  // No-ops. constexpr so the constexpr default constructor stays constexpr.
  constexpr void asan_poison_tail_() noexcept {}
  constexpr void asan_unpoison_all_() noexcept {}
#endif

  // Elements live in one aligned byte buffer reached as a single T[Capacity]
  // array object, so data() + i is in-array arithmetic (the
  // reasoning is in detail/array_storage.hpp). array_storage::data() goes
  // through a reinterpret_cast, which is never constant-evaluable, so the
  // constexpr labels here are effective only outside constant evaluation.
  detail::array_storage<T, (Capacity == 0 ? 1 : Capacity)> storage_;
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
