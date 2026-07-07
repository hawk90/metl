#pragma once

#include "metl/compiler.hpp"

#include <cstdlib>

namespace metl {

/// @brief User assert-handler callback type.
///
/// Invoked with the failed expression text, source file, and line. Even if the
/// handler returns, control never continues past the failed assert: the library
/// calls `std::abort()` immediately afterward (see `set_assert_handler`).
using assert_handler_t = void (*)(const char* expression, const char* file, int line) noexcept;

/// @brief User panic-handler callback type.
///
/// Invoked with a message, source file, and line. Like the assert handler, a
/// returning handler does not resume control flow: `std::abort()` follows.
using panic_handler_t = void (*)(const char* message, const char* file, int line) noexcept;

namespace detail {

[[noreturn]] inline void default_assert_handler(const char*, const char*, int) noexcept {
  std::abort();
}

[[noreturn]] inline void default_panic_handler(const char*, const char*, int) noexcept {
  std::abort();
}

inline assert_handler_t& assert_handler_storage() noexcept {
  // METL_CONST_INIT: the initializer is the address of a function (a constant
  // expression), so the handler slot is guaranteed constant-initialized. This
  // makes the guarantee explicit and rejects any future change that would turn
  // it into dynamic initialization (a static-init-order-fiasco hazard).
  METL_CONST_INIT static assert_handler_t handler = &default_assert_handler;
  return handler;
}

inline panic_handler_t& panic_handler_storage() noexcept {
  METL_CONST_INIT static panic_handler_t handler = &default_panic_handler;
  return handler;
}

[[noreturn]] inline void assertion_failed(const char* expression, const char* file, int line) noexcept {
  assert_handler_storage()(expression, file, line);
  // The customization point allows a user-installed handler that (incorrectly)
  // returns. Guarantee that control never continues past a failed assertion:
  // otherwise every checked precondition in the library would fall through into
  // undefined behavior. std::abort() makes the assert path provably [[noreturn]].
  std::abort();
}

[[noreturn]] inline void panic_failed(const char* message, const char* file, int line) noexcept {
  panic_handler_storage()(message, file, line);
  // As above: a returning panic handler must not resume normal control flow.
  std::abort();
}

}  // namespace detail

/// @brief Install a custom assert handler, returning the previous one.
/// @param handler New handler; if null, the default (`std::abort`) is restored.
/// @return The previously installed handler.
/// @warning The failed-assert path is `[[noreturn]]`: after your handler runs
///          (or if it tries to return) the library calls `std::abort()`. Control
///          can NEVER continue past a failed assert. This is a deliberate
///          UB-safety guarantee.
inline assert_handler_t set_assert_handler(assert_handler_t handler) noexcept {
  assert_handler_t previous = detail::assert_handler_storage();
  detail::assert_handler_storage() = handler != nullptr ? handler : &detail::default_assert_handler;
  return previous;
}

/// @brief Install a custom panic handler, returning the previous one.
/// @param handler New handler; if null, the default (`std::abort`) is restored.
/// @return The previously installed handler.
/// @warning As with asserts, the panic path always aborts: a returning handler
///          does not resume control flow.
inline panic_handler_t set_panic_handler(panic_handler_t handler) noexcept {
  panic_handler_t previous = detail::panic_handler_storage();
  detail::panic_handler_storage() = handler != nullptr ? handler : &detail::default_panic_handler;
  return previous;
}

/// @brief Abort the program with a message, invoking any installed panic handler.
/// @param message Human-readable failure description.
/// @param file Source file (typically `__FILE__`).
/// @param line Source line (typically `__LINE__`).
/// @warning `[[noreturn]]`: ALWAYS aborts via `std::abort()` after the handler
///          runs. Control never returns, even if a user handler tries to return.
[[noreturn]] inline void panic(const char* message, const char* file, int line) noexcept {
  detail::panic_failed(message, file, line);
}

}  // namespace metl
