#pragma once

#include "metl/config.hpp"

#include <type_traits>
#include <utility>

namespace metl {

template <typename>
class delegate;

/// @brief Non-owning, fixed-size callable reference (free function or member).
///
/// Stores only an object pointer plus a static thunk, so it never allocates and
/// holds a reference — not a copy — of the bound target. Construct via the
/// static `from_function` / `bind` helpers, which take the target as a template
/// non-type parameter for zero-overhead dispatch.
/// @warning Non-owning: the bound object must outlive the delegate. A default
///          or nullptr-constructed delegate is empty and must not be called.
template <typename R, typename... Args>
class delegate<R(Args...)> {
 public:
  /// @brief Constructs an empty delegate bound to nothing.
  constexpr delegate() noexcept : object_(nullptr), callback_(nullptr) {}
  /// @brief Constructs an empty delegate from nullptr.
  constexpr delegate(std::nullptr_t) noexcept : object_(nullptr), callback_(nullptr) {}

  /// @brief Binds a free function supplied as a template parameter.
  /// @return A delegate invoking the given free function.
  template <R (*Function)(Args...)>
  METL_NODISCARD static constexpr delegate from_function() noexcept {
    return delegate(nullptr, &invoke_function<Function>);
  }

  /// @brief Binds a non-const member function to an instance.
  /// @param instance Object whose member is called; must outlive the delegate.
  /// @return A delegate invoking `Method` on `instance`.
  template <typename T, R (T::*Method)(Args...)>
  METL_NODISCARD static constexpr delegate bind(T& instance) noexcept {
    return delegate(static_cast<void*>(&instance), &invoke_method<T, Method>);
  }

  /// @brief Binds a const member function to an instance.
  /// @param instance Object whose const member is called; must outlive the delegate.
  /// @return A delegate invoking const `Method` on `instance`.
  template <typename T, R (T::*Method)(Args...) const>
  METL_NODISCARD static constexpr delegate bind(const T& instance) noexcept {
    return delegate(const_cast<void*>(static_cast<const void*>(&instance)), &invoke_const_method<T, Method>);
  }

  /// @brief Tests whether a target is bound.
  METL_NODISCARD constexpr explicit operator bool() const noexcept { return callback_ != nullptr; }
  /// @brief Tests whether a target is bound.
  METL_NODISCARD constexpr bool has_value() const noexcept { return callback_ != nullptr; }

  /// @brief Invokes the bound target.
  /// @pre A target must be bound (has_value() is true); calling an empty
  ///      delegate asserts.
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
