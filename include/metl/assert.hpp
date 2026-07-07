#pragma once

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
  static assert_handler_t handler = &default_assert_handler;
  return handler;
}

inline panic_handler_t& panic_handler_storage() noexcept {
  static panic_handler_t handler = &default_panic_handler;
  return handler;
}

inline void assertion_failed(const char* expression, const char* file, int line) noexcept {
  assert_handler_storage()(expression, file, line);
}

inline void panic_failed(const char* message, const char* file, int line) noexcept {
  panic_handler_storage()(message, file, line);
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

inline void panic(const char* message, const char* file, int line) noexcept {
  detail::panic_failed(message, file, line);
}

}  // namespace metl
