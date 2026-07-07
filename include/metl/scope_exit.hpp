#pragma once

#include "metl/compiler.hpp"

#include <utility>

namespace metl {

// RAII cleanup guard inspired by folly::ScopeGuard and absl::Cleanup.
// In an exceptions-off environment this provides deterministic cleanup
// at scope exit. The stored callable MUST be noexcept because the
// destructor is itself noexcept; we enforce that with a static_assert
// at the point where F is invoked.
template <typename F>
class scope_exit {
 public:
  template <typename G,
            typename =
                typename std::enable_if<!std::is_same<typename std::decay<G>::type, scope_exit>::value>::type>
  explicit scope_exit(G&& func) noexcept : func_(std::forward<G>(func)), active_(true) {}

  scope_exit(scope_exit&& other) noexcept : func_(std::move(other.func_)), active_(other.active_) {
    other.active_ = false;
  }

  scope_exit(const scope_exit&) = delete;
  scope_exit& operator=(const scope_exit&) = delete;
  scope_exit& operator=(scope_exit&&) = delete;

  ~scope_exit() noexcept {
    static_assert(noexcept(std::declval<F&>()()),
                  "metl::scope_exit requires the stored callable to be noexcept");
    if (active_) {
      func_();
    }
  }

  // Disarms the guard so the cleanup function will not be invoked.
  void release() noexcept { active_ = false; }

  // Returns whether the guard is still armed.
  METL_NODISCARD bool active() const noexcept { return active_; }

 private:
  F func_;
  bool active_;
};

// CTAD deduction guide so callers can write `scope_exit guard(lambda);`.
template <typename F>
scope_exit(F) -> scope_exit<F>;

// Convenience factory. Returns a scope_exit decaying the callable type.
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
