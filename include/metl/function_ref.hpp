#pragma once

/// @file
/// @brief Progress guarantees for `metl::function_ref` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | construct, assign, `operator bool` | wait-free, bounded |
///   | `operator()` | one indirect call **plus the referent's own cost** |
///
/// A `function_ref` stores a pointer pair and copies nothing, so binding is
/// independent of what it binds to. Invoking costs one indirect call; the referent
/// is not something this header can bound.

#include "metl/config.hpp"
#include "metl/detail/addressof.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace metl {

template <typename>
class function_ref;

/// @brief Lightweight non-owning reference to any callable, à la std::function_ref.
///
/// Stores only a pointer to the callable (or function pointer) plus a thunk;
/// never allocates and never copies the target. Modeled on P0792.
/// @warning Non-owning: the referenced callable must outlive the function_ref.
/// @warning Rvalue callables are rejected — the rvalue-binding constructor is
///          deleted to prevent dangling references to temporaries. Bind only to
///          lvalues that outlive the function_ref. Function pointers are exempt.
template <typename R, typename... Args>
class function_ref<R(Args...)> {
 public:
  /// @brief Constructs an empty function_ref referencing nothing.
  constexpr function_ref() noexcept : storage_(), callback_(nullptr) {}
  /// @brief Constructs an empty function_ref from nullptr.
  constexpr function_ref(std::nullptr_t) noexcept : storage_(), callback_(nullptr) {}

  /// @brief Binds a free-function pointer.
  /// @param function Non-null function pointer to reference.
  constexpr function_ref(R (*function)(Args...)) noexcept : storage_(function), callback_(&invoke_function) {
    METL_HARDEN(function != nullptr);
  }

  // Bind an lvalue callable (const or non-const). Only lvalue references are
  // accepted: F&& is a forwarding reference, so `is_lvalue_reference<F>` is true
  // exactly when the argument is an lvalue. `Referenced` carries the callable's
  // cv-qualification, so the correct (const or non-const) operator() is invoked.
  template <typename F,
            typename Referenced = std::remove_reference_t<F>,
            typename Decayed = std::decay_t<F>,
            typename = std::enable_if_t<std::is_lvalue_reference_v<F>>,
            typename = std::enable_if_t<!std::is_same_v<Decayed, function_ref>>,
            typename = std::enable_if_t<!std::is_pointer_v<Decayed>>,
            typename = std::enable_if_t<!std::is_member_pointer_v<Decayed>>,
            typename = std::enable_if_t<std::is_invocable_r_v<R, Referenced&, Args...>>>
  /// @brief Binds an lvalue callable (const or non-const), preserving cv-qualification.
  /// @param function Lvalue callable to reference; must outlive this function_ref.
  /// @warning Only lvalues bind here; rvalues select the deleted overload below.
  // METL_LIFETIME_BOUND: function_ref stores a pointer to `function`; the bound
  // callable must outlive this function_ref. clang diagnoses obvious dangling
  // (a callable that dies before the function_ref) at the call site. This
  // complements the deleted rvalue-binding overload below, which already
  // rejects temporaries outright.
  function_ref(F&& function METL_LIFETIME_BOUND) noexcept
      : storage_(const_cast<void*>(static_cast<const void*>(detail::addressof(function)))),
        callback_(&invoke_object<Referenced>) {}

  /// @brief Deleted rvalue-binding overload.
  /// @warning Rejects temporaries to prevent dangling references. Bind
  ///          only to lvalues that outlive the function_ref.
  // Reject rvalue callables: function_ref would store a pointer to a temporary
  // destroyed at the end of the full-expression (a dangling reference). This is
  // STRICTER than std::function_ref (P0792), which accepts temporaries. Function
  // pointers are unaffected — they use the dedicated pointer constructor above.
  template <typename F,
            typename Decayed = std::decay_t<F>,
            typename = std::enable_if_t<!std::is_lvalue_reference_v<F>>,
            typename = std::enable_if_t<!std::is_same_v<Decayed, function_ref>>,
            typename = std::enable_if_t<!std::is_pointer_v<Decayed>>>
  function_ref(F&& function) = delete;

  /// @brief Tests whether a callable is referenced.
  METL_NODISCARD constexpr explicit operator bool() const noexcept { return callback_ != nullptr; }
  /// @brief Tests whether a callable is referenced.
  METL_NODISCARD constexpr bool has_value() const noexcept { return callback_ != nullptr; }

  /// @brief Invokes the referenced callable.
  /// @pre A callable must be bound (has_value() is true); invoking an empty
  ///      function_ref asserts.
  R operator()(Args... args) const {
    METL_HARDEN(callback_ != nullptr);
    return callback_(storage_, std::forward<Args>(args)...);
  }

 private:
  // Two-pointer layout à la std::function_ref (P0792): a single word holding
  // EITHER the bound object's address OR a free-function pointer, plus the
  // thunk. The thunk selects the active union member per binding kind, so the
  // two cases never share storage and no dead slot is threaded through calls.
  //
  // A union (not a shared `void*`) is required: casting between an object
  // `void*` and a function-pointer type is not portable. The active member is
  // always read back through the SAME member it was written through
  // (invoke_function reads `function`, invoke_object reads `object`), so this
  // is well-defined — no cross-member type punning.
  union storage {
    void* object;
    R (*function)(Args...);
    constexpr storage() noexcept : object(nullptr) {}
    constexpr explicit storage(void* obj) noexcept : object(obj) {}
    constexpr explicit storage(R (*fn)(Args...)) noexcept : function(fn) {}
  };

  using callback_type = R (*)(storage, Args&&...);

  // No null check: the pointer constructor, the only one that installs this
  // thunk, refuses a null pointer.
  static R invoke_function(storage bound, Args&&... args) {
    return bound.function(std::forward<Args>(args)...);
  }

  // Referenced carries cv-qualification, so a const callable dispatches to its
  // const operator() and a non-const callable to its non-const operator().
  template <typename Referenced>
  static R invoke_object(storage bound, Args&&... args) {
    auto* function = static_cast<Referenced*>(bound.object);
    if constexpr (std::is_void_v<R>) {
      // Discard a value-returning callable's result in a `void` signature.
      (void)(*function)(std::forward<Args>(args)...);
    } else {
      return (*function)(std::forward<Args>(args)...);
    }
  }

  storage storage_;
  callback_type callback_;
};

}  // namespace metl
