#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/span.hpp"

#include <cstddef>

namespace metl {

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

  constexpr fixed_string() noexcept : storage_{}, size_(0) { storage_[0] = '\0'; }

  fixed_string(const char* text) : storage_{}, size_(0) { (void)assign(text); }

  METL_NODISCARD constexpr iterator begin() noexcept { return storage_; }
  METL_NODISCARD constexpr const_iterator begin() const noexcept { return storage_; }
  METL_NODISCARD constexpr const_iterator cbegin() const noexcept { return storage_; }

  METL_NODISCARD constexpr iterator end() noexcept { return storage_ + size_; }
  METL_NODISCARD constexpr const_iterator end() const noexcept { return storage_ + size_; }
  METL_NODISCARD constexpr const_iterator cend() const noexcept { return storage_ + size_; }

  METL_NODISCARD constexpr pointer data() noexcept { return storage_; }
  METL_NODISCARD constexpr const_pointer data() const noexcept { return storage_; }
  METL_NODISCARD constexpr const_pointer c_str() const noexcept { return storage_; }

  METL_NODISCARD constexpr bool empty() const noexcept { return size_ == 0; }
  METL_NODISCARD constexpr bool full() const noexcept { return size_ == Capacity; }
  METL_NODISCARD constexpr size_type size() const noexcept { return size_; }
  METL_NODISCARD constexpr size_type length() const noexcept { return size_; }
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  METL_NODISCARD constexpr size_type max_size() const noexcept { return Capacity; }

  METL_NODISCARD reference operator[](size_type index) noexcept {
    METL_ASSERT(index < size_);
    return storage_[index];
  }

  METL_NODISCARD const_reference operator[](size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return storage_[index];
  }

  METL_NODISCARD reference front() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_[0];
  }

  METL_NODISCARD const_reference front() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_[0];
  }

  METL_NODISCARD reference back() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_[size_ - 1];
  }

  METL_NODISCARD const_reference back() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_[size_ - 1];
  }

  void clear() noexcept {
    size_ = 0;
    storage_[0] = '\0';
  }

  bool try_push_back(char ch) noexcept {
    if (full()) {
      return false;
    }

    storage_[size_] = ch;
    ++size_;
    storage_[size_] = '\0';
    return true;
  }

  void push_back(char ch) noexcept {
    const bool appended = try_push_back(ch);
    METL_ASSERT(appended);
    (void)appended;
  }

  bool try_pop_back() noexcept {
    if (empty()) {
      return false;
    }

    --size_;
    storage_[size_] = '\0';
    return true;
  }

  void pop_back() noexcept {
    const bool removed = try_pop_back();
    METL_ASSERT(removed);
    (void)removed;
  }

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

  METL_NODISCARD span<char> as_span() noexcept { return span<char>(storage_, size_); }
  METL_NODISCARD span<const char> as_span() const noexcept { return span<const char>(storage_, size_); }

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
