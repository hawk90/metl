#pragma once

#include "metl/config.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace metl {

namespace detail {

// Local std::addressof so function_ref does not pull in all of <memory> for a
// single one-liner. __builtin_addressof is available on every compiler in the
// matrix (gcc/clang/MSVC) and is constant-evaluable; the fallback (only used on
// a hypothetical compiler without it) is the standard operator&-defeating cast.
template <typename T>
inline T* function_ref_addressof(T& arg) noexcept {
#if METL_HAS_BUILTIN(__builtin_addressof) || defined(__GNUC__) || defined(_MSC_VER)
  return __builtin_addressof(arg);
#else
  return reinterpret_cast<T*>(&const_cast<char&>(reinterpret_cast<const volatile char&>(arg)));
#endif
}

}  // namespace detail

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
  /// @brief Binds an lvalue callable (const or non-const), preserving cv-qualification.
  /// @param function Lvalue callable to reference; must outlive this function_ref.
  /// @warning Only lvalues bind here; rvalues select the deleted overload below.
  // METL_LIFETIME_BOUND: function_ref stores a pointer to `function`; the bound
  // callable must outlive this function_ref. clang diagnoses obvious dangling
  // (a callable that dies before the function_ref) at the call site. This
  // complements the deleted rvalue-binding overload below, which already
  // rejects temporaries outright.
  function_ref(F&& function METL_LIFETIME_BOUND) noexcept
      : storage_(const_cast<void*>(static_cast<const void*>(detail::function_ref_addressof(function)))),
        callback_(&invoke_object<Referenced>) {}

  /// @brief Deleted rvalue-binding overload.
  /// @warning Rejects temporaries to prevent dangling references (P0792). Bind
  ///          only to lvalues that outlive the function_ref.
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

  /// @brief Tests whether a callable is referenced.
  METL_NODISCARD constexpr explicit operator bool() const noexcept { return callback_ != nullptr; }
  /// @brief Tests whether a callable is referenced.
  METL_NODISCARD constexpr bool has_value() const noexcept { return callback_ != nullptr; }

  /// @brief Invokes the referenced callable.
  /// @pre A callable must be bound (has_value() is true); invoking an empty
  ///      function_ref asserts.
  R operator()(Args... args) const {
    METL_ASSERT(callback_ != nullptr);
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

  static R invoke_function(storage bound, Args&&... args) {
    METL_ASSERT(bound.function != nullptr);
    return bound.function(std::forward<Args>(args)...);
  }

  // Referenced carries cv-qualification, so a const callable dispatches to its
  // const operator() and a non-const callable to its non-const operator().
  template <typename Referenced>
  static R invoke_object(storage bound, Args&&... args) {
    auto* function = static_cast<Referenced*>(bound.object);
    return (*function)(std::forward<Args>(args)...);
  }

  storage storage_;
  callback_type callback_;
};

}  // namespace metl
