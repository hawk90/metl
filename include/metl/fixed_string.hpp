#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/span.hpp"

#include <cstddef>

namespace metl {

/// Null-terminated character string with a compile-time FIXED capacity.
///
/// Stores up to `Capacity` characters (plus a terminating '\0') inline; performs
/// NO heap allocation. Overflowing operations that assert (constructor,
/// push_back) abort by default; the bool-returning members (assign, append,
/// try_push_back, try_pop_back) report overflow/underflow instead. Not
/// thread-safe.
///
/// @tparam Capacity Maximum number of characters, excluding the terminator.
template <std::size_t Capacity>
class fixed_string {
 public:
  using value_type = char;
  using size_type = std::size_t;
  using pointer = char*;
  using const_pointer = const char*;
  using reference = char&;
  using const_reference = const char&;
  using iterator = char*;
  using const_iterator = const char*;

  /// Constructs an empty string.
  constexpr fixed_string() noexcept : storage_{}, size_(0) { storage_[0] = '\0'; }

  /// Constructs from a null-terminated C string.
  /// @pre `text` fits in Capacity. On overflow this asserts (the default handler
  /// aborts) rather than silently truncating. Callers needing a recoverable,
  /// non-asserting path should default-construct and call assign(), which reports
  /// overflow via its bool result.
  fixed_string(const char* text) : storage_{}, size_(0) {
    const bool assigned = assign(text);
    METL_ASSERT(assigned);
    (void)assigned;
  }

  /// Returns an iterator to the first character.
  METL_NODISCARD constexpr iterator begin() noexcept { return storage_; }
  METL_NODISCARD constexpr const_iterator begin() const noexcept { return storage_; }
  METL_NODISCARD constexpr const_iterator cbegin() const noexcept { return storage_; }

  /// Returns an iterator one past the last character (at the terminator).
  METL_NODISCARD constexpr iterator end() noexcept { return storage_ + size_; }
  METL_NODISCARD constexpr const_iterator end() const noexcept { return storage_ + size_; }
  METL_NODISCARD constexpr const_iterator cend() const noexcept { return storage_ + size_; }

  /// Returns a pointer to the null-terminated character buffer.
  METL_NODISCARD constexpr pointer data() noexcept { return storage_; }
  /// Returns a pointer to the null-terminated character buffer.
  METL_NODISCARD constexpr const_pointer data() const noexcept { return storage_; }
  /// Returns the null-terminated C string.
  METL_NODISCARD constexpr const_pointer c_str() const noexcept { return storage_; }

  /// Returns true if the string has zero length.
  METL_NODISCARD constexpr bool empty() const noexcept { return size_ == 0; }
  /// Returns true if the string has reached its fixed capacity.
  METL_NODISCARD constexpr bool full() const noexcept { return size_ == Capacity; }
  /// Returns the number of characters (excluding the terminator).
  METL_NODISCARD constexpr size_type size() const noexcept { return size_; }
  /// Returns the number of characters (excluding the terminator).
  METL_NODISCARD constexpr size_type length() const noexcept { return size_; }
  /// Returns the fixed capacity (`Capacity`), excluding the terminator.
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  /// Returns the fixed capacity (`Capacity`); always equal to capacity().
  METL_NODISCARD constexpr size_type max_size() const noexcept { return Capacity; }

  /// Accesses the character at `index`.
  /// @pre `index < size()`; out-of-range asserts and aborts (does not throw).
  METL_NODISCARD reference operator[](size_type index) noexcept {
    METL_ASSERT(index < size_);
    return storage_[index];
  }

  /// Accesses the character at `index`.
  /// @pre `index < size()`; out-of-range asserts and aborts (does not throw).
  METL_NODISCARD const_reference operator[](size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return storage_[index];
  }

  /// Returns a reference to the first character.
  /// @pre String is non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference front() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_[0];
  }

  /// Returns a reference to the first character.
  /// @pre String is non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference front() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_[0];
  }

  /// Returns a reference to the last character.
  /// @pre String is non-empty; asserts and aborts otherwise.
  METL_NODISCARD reference back() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_[size_ - 1];
  }

  /// Returns a reference to the last character.
  /// @pre String is non-empty; asserts and aborts otherwise.
  METL_NODISCARD const_reference back() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_[size_ - 1];
  }

  /// Clears the string to zero length.
  void clear() noexcept {
    size_ = 0;
    storage_[0] = '\0';
  }

  /// Appends `ch` if there is room.
  /// @return true on success; false if the string is full (no assert).
  bool try_push_back(char ch) noexcept {
    if (full()) {
      return false;
    }

    storage_[size_] = ch;
    ++size_;
    storage_[size_] = '\0';
    return true;
  }

  /// Appends `ch`.
  /// @pre String is not full; overflow asserts and aborts. Use try_push_back for
  /// a non-asserting path.
  void push_back(char ch) noexcept {
    const bool appended = try_push_back(ch);
    METL_ASSERT(appended);
    (void)appended;
  }

  /// Removes the last character if the string is non-empty.
  /// @return true on success; false if the string is empty (no assert).
  bool try_pop_back() noexcept {
    if (empty()) {
      return false;
    }

    --size_;
    storage_[size_] = '\0';
    return true;
  }

  /// Removes the last character.
  /// @pre String is non-empty; asserts and aborts otherwise.
  void pop_back() noexcept {
    const bool removed = try_pop_back();
    METL_ASSERT(removed);
    (void)removed;
  }

  /// Replaces the contents with the null-terminated string `text`.
  /// @return true on success; false if `text` does not fit (contents unchanged).
  /// @pre `text != nullptr`.
  bool assign(const char* text) noexcept {
    METL_ASSERT(text != nullptr);

    const size_type input_size = string_length(text);
    if (input_size > Capacity) {
      return false;
    }

    for (size_type i = 0; i < input_size; ++i) {
      storage_[i] = text[i];
    }

    size_ = input_size;
    storage_[size_] = '\0';
    return true;
  }

  /// Appends the null-terminated string `text`.
  /// @return true on success; false if it does not fit (contents unchanged).
  /// @pre `text != nullptr`.
  bool append(const char* text) noexcept {
    METL_ASSERT(text != nullptr);

    const size_type input_size = string_length(text);
    if (!can_append(input_size)) {
      return false;
    }

    for (size_type i = 0; i < input_size; ++i) {
      storage_[size_ + i] = text[i];
    }

    size_ += input_size;
    storage_[size_] = '\0';
    return true;
  }

  /// Appends the characters viewed by `text`.
  /// @return true on success; false if they do not fit (contents unchanged).
  bool append(span<const char> text) noexcept {
    if (!can_append(text.size())) {
      return false;
    }

    for (size_type i = 0; i < text.size(); ++i) {
      storage_[size_ + i] = text[i];
    }

    size_ += text.size();
    storage_[size_] = '\0';
    return true;
  }

  /// Returns a span viewing the current characters (excluding the terminator).
  METL_NODISCARD span<char> as_span() noexcept { return span<char>(storage_, size_); }
  /// Returns a read-only span viewing the current characters.
  METL_NODISCARD span<const char> as_span() const noexcept { return span<const char>(storage_, size_); }

  /// Returns true if both strings have equal length and characters.
  friend bool operator==(const fixed_string& lhs, const fixed_string& rhs) noexcept {
    if (lhs.size_ != rhs.size_) {
      return false;
    }

    for (size_type i = 0; i < lhs.size_; ++i) {
      if (lhs.storage_[i] != rhs.storage_[i]) {
        return false;
      }
    }

    return true;
  }

  /// Returns true if the strings differ in length or any character.
  friend bool operator!=(const fixed_string& lhs, const fixed_string& rhs) noexcept { return !(lhs == rhs); }

 private:
  static size_type string_length(const char* text) noexcept {
    size_type length = 0;
    while (text[length] != '\0') {
      ++length;
    }
    return length;
  }

  constexpr bool can_append(size_type count) const noexcept { return count <= (Capacity - size_); }

  char storage_[Capacity + 1];
  size_type size_;
};

}  // namespace metl
