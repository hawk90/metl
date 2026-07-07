#pragma once

#include "metl/assert.hpp"
#include "metl/compiler.hpp"
#include "metl/optimization.hpp"

#ifndef METL_NO_EXCEPTIONS
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define METL_NO_EXCEPTIONS 0
#else
#define METL_NO_EXCEPTIONS 1
#endif
#endif

// METL_ASSERT — the always-on, hardened precondition check. Active in every
// build configuration (including release / NDEBUG); a failure routes through
// the [[noreturn]] assert path in <metl/assert.hpp>. The failing branch is
// marked METL_PREDICT_FALSE so the success path stays the straight-line case.
#ifndef METL_ASSERT
#define METL_ASSERT(expr)                                          \
  do {                                                             \
    if (METL_PREDICT_FALSE(!(expr))) {                             \
      ::metl::detail::assertion_failed(#expr, __FILE__, __LINE__); \
    }                                                              \
  } while (false)
#endif

// METL_DASSERT — debug-only assertion (a DCHECK). Identical to METL_ASSERT when
// debug checks are enabled (`!NDEBUG`, or METL_DEBUG defined), and compiles to
// nothing — evaluating `expr` only in an unevaluated context so it neither runs
// side effects nor triggers unused warnings — otherwise.
//
// Use METL_DASSERT for checks that are valuable during development but too
// costly for always-on hardening; use METL_ASSERT for security/safety-critical
// preconditions that must hold in shipping builds. METL_ASSERT is never
// downgraded to METL_DASSERT.
#ifndef METL_DASSERT
#if defined(METL_DEBUG) || !defined(NDEBUG)
#define METL_DASSERT(expr) METL_ASSERT(expr)
#else
#define METL_DASSERT(expr)        \
  do {                            \
    (void)sizeof((expr) ? 1 : 0); \
  } while (false)
#endif
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
