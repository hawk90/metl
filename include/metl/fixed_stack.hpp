#pragma once

#include "metl/compiler.hpp"
#include "metl/fixed_vector.hpp"

#include <cstddef>
#include <utility>

namespace metl {

template <typename T, std::size_t Capacity>
class fixed_stack {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = const T&;

  constexpr fixed_stack() noexcept = default;

  METL_NODISCARD bool empty() const noexcept { return storage_.empty(); }
  METL_NODISCARD bool full() const noexcept { return storage_.full(); }
  METL_NODISCARD size_type size() const noexcept { return storage_.size(); }
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  METL_NODISCARD reference top() noexcept { return storage_.back(); }
  METL_NODISCARD const_reference top() const noexcept { return storage_.back(); }

  template <typename... Args>
  METL_NODISCARD bool try_emplace(Args&&... args) {
    return storage_.try_emplace_back(std::forward<Args>(args)...);
  }

  template <typename... Args>
  reference emplace(Args&&... args) {
    return storage_.emplace_back(std::forward<Args>(args)...);
  }

  METL_NODISCARD bool try_push(const T& value) { return storage_.try_push_back(value); }
  METL_NODISCARD bool try_push(T&& value) { return storage_.try_push_back(static_cast<T&&>(value)); }

  reference push(const T& value) { return storage_.push_back(value); }
  reference push(T&& value) { return storage_.push_back(static_cast<T&&>(value)); }

  void pop() noexcept { storage_.pop_back(); }

  void clear() noexcept { storage_.clear(); }

 private:
  fixed_vector<T, Capacity> storage_;
};

}  // namespace metl
