#pragma once

// Optimization / codegen hints (abseil `absl/base/optimization.h` style).
//
// Branch-prediction, assumption and cache-line helpers. Every macro has a
// portable fallback: the branch-prediction macros collapse to the plain
// boolean value, METL_ASSUME collapses to a no-op, and the cache-line helpers
// use standard `alignas`. Nothing here changes program semantics — only the
// code the compiler is allowed to generate.

#include "metl/compiler.hpp"

#include <cstddef>

// METL_PREDICT_TRUE(expr) / METL_PREDICT_FALSE(expr)
//   Hint the likely value of a boolean expression to the optimizer. These are
//   expressions that evaluate to the same bool as `expr`; only the generated
//   branch layout differs. Do not place side effects in `expr` and assume they
//   are elided — they are not; the expression is evaluated exactly once.
#if METL_HAS_BUILTIN(__builtin_expect) || METL_COMPILER_GCC || METL_COMPILER_CLANG
#define METL_PREDICT_TRUE(expr) (__builtin_expect(static_cast<bool>(expr), true))
#define METL_PREDICT_FALSE(expr) (__builtin_expect(static_cast<bool>(expr), false))
#else
#define METL_PREDICT_TRUE(expr) (static_cast<bool>(expr))
#define METL_PREDICT_FALSE(expr) (static_cast<bool>(expr))
#endif

// METL_ASSUME(cond)
//   Tell the optimizer it may assume `cond` holds. Reaching this macro with a
//   false `cond` is undefined behavior, so use it only for facts you can prove.
//   `cond` may be evaluated; keep it side-effect free.
#if METL_COMPILER_MSVC
#define METL_ASSUME(cond) __assume(cond)
#elif METL_HAS_BUILTIN(__builtin_assume)
#define METL_ASSUME(cond) __builtin_assume(static_cast<bool>(cond))
#elif METL_HAS_BUILTIN(__builtin_unreachable) || METL_COMPILER_GCC || METL_COMPILER_CLANG
#define METL_ASSUME(cond) (static_cast<bool>(cond) ? void(0) : __builtin_unreachable())
#else
#define METL_ASSUME(cond) (static_cast<void>(0))
#endif

// METL_CACHELINE_SIZE — the target's assumed hardware cache-line size in bytes.
//   Used to pad/align hot data so independent fields land on distinct lines and
//   avoid false sharing. Overridable by defining METL_CACHELINE_SIZE upfront.
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
#ifndef METL_CACHELINE_ALIGNED
#define METL_CACHELINE_ALIGNED alignas(METL_CACHELINE_SIZE)
#endif
