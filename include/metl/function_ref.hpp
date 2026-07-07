#pragma once

#include "metl/config.hpp"

#include <cstddef>
#include <memory>
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

  // Bind an lvalue callable (const or non-const). Only lvalue references are
  // accepted: F&& is a forwarding reference, so `is_lvalue_reference<F>` is true
  // exactly when the argument is an lvalue. `Referenced` carries the callable's
  // cv-qualification, so the correct (const or non-const) operator() is invoked.
  template <typename F,
            typename Referenced = typename std::remove_reference<F>::type,
            typename Decayed = typename std::decay<F>::type,
            typename = typename std::enable_if<std::is_lvalue_reference<F>::value>::type,
            typename = typename std::enable_if<!std::is_same<Decayed, function_ref>::value>::type,
            typename = typename std::enable_if<!std::is_pointer<Decayed>::value>::type,
            typename = typename std::enable_if<std::is_invocable_r<R, Referenced&, Args...>::value>::type>
  function_ref(F&& function) noexcept
      : object_(const_cast<void*>(static_cast<const void*>(std::addressof(function)))),
        function_(nullptr),
        callback_(&invoke_object<Referenced>) {}

  // Reject rvalue callables: function_ref would store a pointer to a temporary
  // destroyed at the end of the full-expression (a dangling reference). This
  // mirrors std::function_ref (P0792), which deletes rvalue binding. Function
  // pointers are unaffected — they use the dedicated pointer constructor above.
  template <typename F,
            typename Decayed = typename std::decay<F>::type,
            typename = typename std::enable_if<!std::is_lvalue_reference<F>::value>::type,
            typename = typename std::enable_if<!std::is_same<Decayed, function_ref>::value>::type,
            typename = typename std::enable_if<!std::is_pointer<Decayed>::value>::type>
  function_ref(F&& function) = delete;

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

  // Referenced carries cv-qualification, so a const callable dispatches to its
  // const operator() and a non-const callable to its non-const operator().
  template <typename Referenced>
  static R invoke_object(void* object, R (*)(Args...), Args&&... args) {
    auto* function = static_cast<Referenced*>(object);
    return (*function)(std::forward<Args>(args)...);
  }

  void* object_;
  R (*function_)(Args...);
  callback_type callback_;
};

}  // namespace metl
