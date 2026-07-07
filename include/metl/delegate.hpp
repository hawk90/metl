#pragma once

#include "metl/config.hpp"

#include <type_traits>
#include <utility>

namespace metl {

template <typename>
class delegate;

template <typename R, typename... Args>
class delegate<R(Args...)> {
 public:
  constexpr delegate() noexcept : object_(nullptr), callback_(nullptr) {}
  constexpr delegate(std::nullptr_t) noexcept : object_(nullptr), callback_(nullptr) {}

  template <R (*Function)(Args...)>
  METL_NODISCARD static constexpr delegate from_function() noexcept {
    return delegate(nullptr, &invoke_function<Function>);
  }

  template <typename T, R (T::*Method)(Args...)>
  METL_NODISCARD static constexpr delegate bind(T& instance) noexcept {
    return delegate(static_cast<void*>(&instance), &invoke_method<T, Method>);
  }

  template <typename T, R (T::*Method)(Args...) const>
  METL_NODISCARD static constexpr delegate bind(const T& instance) noexcept {
    return delegate(const_cast<void*>(static_cast<const void*>(&instance)), &invoke_const_method<T, Method>);
  }

  METL_NODISCARD constexpr explicit operator bool() const noexcept { return callback_ != nullptr; }
  METL_NODISCARD constexpr bool has_value() const noexcept { return callback_ != nullptr; }

  R operator()(Args... args) const {
    METL_ASSERT(callback_ != nullptr);
    return callback_(object_, std::forward<Args>(args)...);
  }

 private:
  using callback_type = R (*)(void*, Args&&...);

  constexpr delegate(void* object, callback_type callback) noexcept : object_(object), callback_(callback) {}

  template <R (*Function)(Args...)>
  static R invoke_function(void*, Args&&... args) {
    return Function(std::forward<Args>(args)...);
  }

  template <typename T, R (T::*Method)(Args...)>
  static R invoke_method(void* object, Args&&... args) {
    return (static_cast<T*>(object)->*Method)(std::forward<Args>(args)...);
  }

  template <typename T, R (T::*Method)(Args...) const>
  static R invoke_const_method(void* object, Args&&... args) {
    return (static_cast<const T*>(object)->*Method)(std::forward<Args>(args)...);
  }

  void* object_;
  callback_type callback_;
};

}  // namespace metl
