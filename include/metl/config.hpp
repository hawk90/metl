#pragma once

#include "metl/assert.hpp"
#include "metl/compiler.hpp"

#ifndef METL_NO_EXCEPTIONS
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define METL_NO_EXCEPTIONS 0
#else
#define METL_NO_EXCEPTIONS 1
#endif
#endif

#ifndef METL_ASSERT
#define METL_ASSERT(expr)                                          \
  do {                                                             \
    if (!(expr)) {                                                 \
      ::metl::detail::assertion_failed(#expr, __FILE__, __LINE__); \
    }                                                              \
  } while (false)
#endif

#ifndef METL_PANIC
#define METL_PANIC(message)                       \
  do {                                            \
    ::metl::panic((message), __FILE__, __LINE__); \
  } while (false)
#endif

namespace metl {

inline constexpr int version_major = 0;
inline constexpr int version_minor = 1;
inline constexpr int version_patch = 0;

}  // namespace metl
