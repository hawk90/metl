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

// METL_CACHELINE_SIZE — the cache-line size METL assumes for the target, in
// bytes. spsc_queue, spsc_byte_ring and mpmc_queue align their producer index,
// consumer index and storage to it, so it sets those types' sizeof and layout.
//
// It is a fixed value per target, not a setting: two translation units that
// disagreed on it would define those types twice with different layouts, an
// ODR violation. Defining it before including METL is an #error.
//
//   Cortex-M (ARMv6-M/v7-M/v8-M)   8    The M profile has no coherent multi-core
//                                       cache to share a line across, so there is
//                                       no false sharing to pad against; 8 keeps
//                                       64-bit atomics aligned. spsc_queue<int, 4>
//                                       is 32 B rather than 192.
//   Apple arm64                     128  hw.cachelinesize on Apple silicon.
//   ppc64                           128
//   s390x                           256
//   everything else                 64
//
// Not std::hardware_destructive_interference_size: libstdc++ warns that its
// value is not ABI-stable, and C++17 compilers do not all provide it.
/// @brief Cache-line size in bytes that METL assumes for the target. Fixed per
///        target; predefining it is an error.
#ifdef METL_CACHELINE_SIZE
#error \
    "METL_CACHELINE_SIZE must not be predefined: it sets the layout of spsc_queue, spsc_byte_ring and mpmc_queue, so it is fixed per target"
#endif
#if defined(__ARM_ARCH_PROFILE) && __ARM_ARCH_PROFILE == 'M'
#define METL_CACHELINE_SIZE 8
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#define METL_CACHELINE_SIZE 128
#elif defined(__powerpc64__) || defined(__PPC64__) || defined(__ppc64__)
#define METL_CACHELINE_SIZE 128
#elif defined(__s390x__)
#define METL_CACHELINE_SIZE 256
#else
#define METL_CACHELINE_SIZE 64
#endif

// METL_CACHELINE_ALIGNED — align a variable/member to METL_CACHELINE_SIZE. Uses
// standard `alignas`, so it is portable to every C++17 compiler. Like the size,
// it is not a setting.
/// @brief Align a variable/member to a full cache line (portable `alignas`).
#ifdef METL_CACHELINE_ALIGNED
#error \
    "METL_CACHELINE_ALIGNED must not be predefined: it is alignas(METL_CACHELINE_SIZE), which is fixed per target"
#endif
#define METL_CACHELINE_ALIGNED alignas(METL_CACHELINE_SIZE)

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
