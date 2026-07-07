#pragma once

#include "metl/compiler.hpp"

#include <utility>

namespace metl {

/// @brief RAII cleanup guard that runs a stored callable when the scope ends.
///
/// Inspired by folly::ScopeGuard and absl::Cleanup. Owns the callable by value
/// and, unless disarmed, invokes it exactly once from the destructor. Move
/// transfers the armed state; copy and assignment are disabled.
/// @tparam F Callable type stored by value; MUST be noexcept-invocable.
/// @warning The stored callable must be noexcept because the destructor is
///          noexcept; a static_assert enforces this where F is invoked.
template <typename F>
class scope_exit {
 public:
  /// @brief Construct an armed guard owning a copy/move of the callable.
  /// @param func Callable to invoke at scope exit.
  template <typename G,
            typename =
                typename std::enable_if<!std::is_same<typename std::decay<G>::type, scope_exit>::value>::type>
  explicit scope_exit(G&& func) noexcept : func_(std::forward<G>(func)), active_(true) {}

  /// @brief Move constructor; transfers the armed state and disarms the source.
  scope_exit(scope_exit&& other) noexcept : func_(std::move(other.func_)), active_(other.active_) {
    other.active_ = false;
  }

  scope_exit(const scope_exit&) = delete;
  scope_exit& operator=(const scope_exit&) = delete;
  scope_exit& operator=(scope_exit&&) = delete;

  /// @brief Invokes the stored callable if the guard is still armed.
  ~scope_exit() noexcept {
    static_assert(noexcept(std::declval<F&>()()),
                  "metl::scope_exit requires the stored callable to be noexcept");
    if (active_) {
      func_();
    }
  }

  /// @brief Disarms the guard so the cleanup function will not be invoked.
  void release() noexcept { active_ = false; }

  /// @brief Returns whether the guard is still armed.
  /// @return true while the callable is scheduled to run at scope exit.
  METL_NODISCARD bool active() const noexcept { return active_; }

 private:
  F func_;
  bool active_;
};

/// @brief CTAD deduction guide so callers can write `scope_exit guard(lambda);`.
template <typename F>
scope_exit(F) -> scope_exit<F>;

/// @brief Convenience factory that builds a scope_exit with a decayed callable.
/// @param func Callable to invoke at scope exit.
/// @return An armed scope_exit owning a decayed copy of `func`.
template <typename F>
METL_NODISCARD auto make_scope_exit(F&& func) noexcept -> scope_exit<typename std::decay<F>::type> {
  return scope_exit<typename std::decay<F>::type>(std::forward<F>(func));
}

namespace detail {

// Tag dispatch helper so METL_SCOPE_EXIT can write `auto var = ... + [&]() {...};`.
struct scope_exit_tag {};

template <typename F>
auto operator+(scope_exit_tag, F&& func) noexcept -> scope_exit<typename std::decay<F>::type> {
  return scope_exit<typename std::decay<F>::type>(std::forward<F>(func));
}

}  // namespace detail

}  // namespace metl

#define METL_SCOPE_EXIT_CONCAT_INNER(a, b) a##b
#define METL_SCOPE_EXIT_CONCAT(a, b) METL_SCOPE_EXIT_CONCAT_INNER(a, b)

// Creates an anonymous RAII guard executing `expr` when the enclosing
// scope ends. The expression is wrapped in a noexcept lambda capturing
// by reference. Usage: METL_SCOPE_EXIT(file.close());
#define METL_SCOPE_EXIT(...)                                   \
  auto METL_SCOPE_EXIT_CONCAT(metl_scope_exit_, __COUNTER__) = \
      ::metl::detail::scope_exit_tag{} + [&]() noexcept { __VA_ARGS__; }
