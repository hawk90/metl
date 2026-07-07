#pragma once

#include "metl/config.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace metl {

template <typename>
class function_ref;

template <typename R, typename... Args>
class function_ref<R(Args...)> {
 public:
  constexpr function_ref() noexcept : object_(nullptr), function_(nullptr), callback_(nullptr) {}
  constexpr function_ref(std::nullptr_t) noexcept
      : object_(nullptr), function_(nullptr), callback_(nullptr) {}

  constexpr function_ref(R (*function)(Args...)) noexcept
      : object_(nullptr), function_(function), callback_(&invoke_function) {
    METL_ASSERT(function != nullptr);
  }

  template <typename F,
            typename Decayed = typename std::decay<F>::type,
            typename = typename std::enable_if<!std::is_same<Decayed, function_ref>::value>::type,
            typename = typename std::enable_if<std::is_invocable_r<R, F&, Args...>::value>::type>
  function_ref(F& function) noexcept
      : object_(static_cast<void*>(&function)), function_(nullptr), callback_(&invoke_object<F>) {}

  template <typename F,
            typename Decayed = typename std::decay<F>::type,
            typename = typename std::enable_if<!std::is_same<Decayed, function_ref>::value>::type,
            typename = typename std::enable_if<std::is_invocable_r<R, const F&, Args...>::value>::type>
  function_ref(const F& function) noexcept
      : object_(const_cast<void*>(static_cast<const void*>(&function))),
        function_(nullptr),
        callback_(&invoke_const_object<F>) {}

  METL_NODISCARD constexpr explicit operator bool() const noexcept { return callback_ != nullptr; }
  METL_NODISCARD constexpr bool has_value() const noexcept { return callback_ != nullptr; }

  R operator()(Args... args) const {
    METL_ASSERT(callback_ != nullptr);
    return callback_(object_, function_, std::forward<Args>(args)...);
  }

 private:
  using callback_type = R (*)(void*, R (*)(Args...), Args&&...);

  static R invoke_function(void*, R (*function)(Args...), Args&&... args) {
    METL_ASSERT(function != nullptr);
    return function(std::forward<Args>(args)...);
  }

  template <typename F>
  static R invoke_object(void* object, R (*)(Args...), Args&&... args) {
    auto* function = static_cast<F*>(object);
    return (*function)(std::forward<Args>(args)...);
  }

  template <typename F>
  static R invoke_const_object(void* object, R (*)(Args...), Args&&... args) {
    auto* function = static_cast<const F*>(object);
    return (*function)(std::forward<Args>(args)...);
  }

  void* object_;
  R (*function_)(Args...);
  callback_type callback_;
};

}  // namespace metl
