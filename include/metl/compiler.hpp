#pragma once

#if defined(_MSC_VER)
#define METL_COMPILER_MSVC 1
#define METL_COMPILER_MSVC_VERSION _MSC_VER
#else
#define METL_COMPILER_MSVC 0
#define METL_COMPILER_MSVC_VERSION 0
#endif

#if defined(__clang__)
#define METL_COMPILER_CLANG 1
#else
#define METL_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define METL_COMPILER_GCC 1
#else
#define METL_COMPILER_GCC 0
#endif

// MSVC leaves __cplusplus at 199711L unless the user passes /Zc:__cplusplus;
// _MSVC_LANG always carries the real standard. Without this, METL_CXX_STANDARD
// would read C++98 on every MSVC build.
#if defined(_MSVC_LANG)
#define METL_CXX_STANDARD _MSVC_LANG
#elif defined(__cplusplus)
#define METL_CXX_STANDARD __cplusplus
#else
#define METL_CXX_STANDARD 0L
#endif

#if defined(__has_builtin)
#define METL_HAS_BUILTIN(x) __has_builtin(x)
#else
#define METL_HAS_BUILTIN(x) 0
#endif

#if defined(__has_cpp_attribute)
#define METL_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define METL_HAS_CPP_ATTRIBUTE(x) 0
#endif

#if defined(__has_attribute)
#define METL_HAS_ATTRIBUTE(x) __has_attribute(x)
#else
#define METL_HAS_ATTRIBUTE(x) 0
#endif

// config.h-style feature detection (abseil `absl/base/config.h`). Each wrapper
// has a safe fallback so `#if METL_HAVE_*(x)` is always well-formed, even on
// toolchains lacking the underlying `__has_*` operator.

// (Builtin availability is spelled METL_HAS_BUILTIN, defined above alongside
// METL_HAS_CPP_ATTRIBUTE / METL_HAS_ATTRIBUTE; there is no separate
// METL_HAVE_BUILTIN, which would be a byte-for-byte duplicate.)

// METL_HAVE_FEATURE(x) — is clang language/sanitizer feature `x` enabled?
// Defined to 0 on compilers without __has_feature (e.g. gcc, msvc).
#if defined(__has_feature)
#define METL_HAVE_FEATURE(x) __has_feature(x)
#else
#define METL_HAVE_FEATURE(x) 0
#endif

// METL_NODISCARD and the rest of the attribute layer live in
// <metl/attributes.hpp>, included at the end of this header so every
// translation unit that includes <metl/compiler.hpp> continues to see them.

#if METL_COMPILER_MSVC
#define METL_FORCE_INLINE __forceinline
#elif METL_HAS_ATTRIBUTE(always_inline) || METL_COMPILER_GCC || METL_COMPILER_CLANG
#define METL_FORCE_INLINE inline __attribute__((always_inline))
#else
#define METL_FORCE_INLINE inline
#endif

#if METL_COMPILER_MSVC
#define METL_UNREACHABLE() __assume(0)
#elif METL_HAS_BUILTIN(__builtin_unreachable) || METL_COMPILER_GCC || METL_COMPILER_CLANG
#define METL_UNREACHABLE() __builtin_unreachable()
#else
#define METL_UNREACHABLE() ((void)0)
#endif

// Attribute layer (METL_NODISCARD, METL_LIFETIME_BOUND, METL_CONST_INIT, ...).
// Included last so it can use the detection macros defined above; guarded by
// #pragma once so the mutual include with attributes.hpp is safe.
#include "metl/attributes.hpp"
