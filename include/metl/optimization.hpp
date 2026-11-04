#pragma once

// Optimization / codegen hints (abseil `absl/base/optimization.h` style).
//
// Branch-prediction and cache-line helpers. Every macro has a portable
// fallback: the branch-prediction macros collapse to the plain boolean value,
// and the cache-line helpers use standard `alignas`. Nothing here changes
// program semantics — only the code the compiler is allowed to generate.

#include "metl/compiler.hpp"

#include <cstddef>

// METL_PREDICT_TRUE(expr) / METL_PREDICT_FALSE(expr)
//   Hint the likely value of a boolean expression to the optimizer. These are
//   expressions that evaluate to the same bool as `expr`; only the generated
//   branch layout differs. Do not place side effects in `expr` and assume they
//   are elided — they are not; the expression is evaluated exactly once.
/// @brief Branch hint: the boolean `expr` is likely true. Value-equal to `expr`;
///        only branch layout changes. Keep `expr` side-effect free.
#if METL_HAS_BUILTIN(__builtin_expect) || METL_COMPILER_GCC || METL_COMPILER_CLANG
#define METL_PREDICT_TRUE(expr) (__builtin_expect(static_cast<bool>(expr), true))
/// @brief Branch hint: the boolean `expr` is likely false. Value-equal to `expr`.
#define METL_PREDICT_FALSE(expr) (__builtin_expect(static_cast<bool>(expr), false))
#else
#define METL_PREDICT_TRUE(expr) (static_cast<bool>(expr))
#define METL_PREDICT_FALSE(expr) (static_cast<bool>(expr))
#endif

// METL_CACHELINE_SIZE — the target's assumed hardware cache-line size in bytes.
//   Used to pad/align hot data so independent fields land on distinct lines and
//   avoid false sharing. Overridable by defining METL_CACHELINE_SIZE upfront.
/// @brief Assumed hardware cache-line size in bytes for the target. Used to pad
///        or align hot data to avoid false sharing. Overridable by predefining it.
#ifndef METL_CACHELINE_SIZE
#if defined(__powerpc64__) || defined(__PPC64__) || defined(__ppc64__)
#define METL_CACHELINE_SIZE 128
#elif defined(__s390x__)
#define METL_CACHELINE_SIZE 256
#else
#define METL_CACHELINE_SIZE 64
#endif
#endif

// METL_CACHELINE_ALIGNED — align a variable/member to a full cache line. Uses
// standard `alignas`, so it is portable to every C++17 compiler.
/// @brief Align a variable/member to a full cache line (portable `alignas`).
#ifndef METL_CACHELINE_ALIGNED
#define METL_CACHELINE_ALIGNED alignas(METL_CACHELINE_SIZE)
#endif

// METL_CACHELINE_PADDED_BEGIN / _END bracket a class whose members are
// METL_CACHELINE_ALIGNED. MSVC reports the padding that alignment exists to
// create (C4324, a level-4 warning), so a consumer building with /W4 /WX could
// not instantiate the class. Nothing elsewhere: other compilers do not warn.
#if defined(_MSC_VER) && !defined(__clang__)
#define METL_CACHELINE_PADDED_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4324))
#define METL_CACHELINE_PADDED_END __pragma(warning(pop))
#else
#define METL_CACHELINE_PADDED_BEGIN
#define METL_CACHELINE_PADDED_END
#endif
