#pragma once

#include "metl/compiler.hpp"

#include <cstdlib>

namespace metl {

using assert_handler_t = void (*)(const char* expression, const char* file, int line) noexcept;
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

inline assert_handler_t set_assert_handler(assert_handler_t handler) noexcept {
  assert_handler_t previous = detail::assert_handler_storage();
  detail::assert_handler_storage() = handler != nullptr ? handler : &detail::default_assert_handler;
  return previous;
}

inline panic_handler_t set_panic_handler(panic_handler_t handler) noexcept {
  panic_handler_t previous = detail::panic_handler_storage();
  detail::panic_handler_storage() = handler != nullptr ? handler : &detail::default_panic_handler;
  return previous;
}

[[noreturn]] inline void panic(const char* message, const char* file, int line) noexcept {
  detail::panic_failed(message, file, line);
}

}  // namespace metl
