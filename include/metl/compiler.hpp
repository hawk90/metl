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
#define METL_COMPILER_CLANG_VERSION_MAJOR __clang_major__
#define METL_COMPILER_CLANG_VERSION_MINOR __clang_minor__
#define METL_COMPILER_CLANG_VERSION_PATCH __clang_patchlevel__
#else
#define METL_COMPILER_CLANG 0
#define METL_COMPILER_CLANG_VERSION_MAJOR 0
#define METL_COMPILER_CLANG_VERSION_MINOR 0
#define METL_COMPILER_CLANG_VERSION_PATCH 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define METL_COMPILER_GCC 1
#define METL_COMPILER_GCC_VERSION_MAJOR __GNUC__
#define METL_COMPILER_GCC_VERSION_MINOR __GNUC_MINOR__
#define METL_COMPILER_GCC_VERSION_PATCH __GNUC_PATCHLEVEL__
#else
#define METL_COMPILER_GCC 0
#define METL_COMPILER_GCC_VERSION_MAJOR 0
#define METL_COMPILER_GCC_VERSION_MINOR 0
#define METL_COMPILER_GCC_VERSION_PATCH 0
#endif

#if defined(__cplusplus)
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

#if METL_HAS_CPP_ATTRIBUTE(nodiscard) >= 201603L
#define METL_NODISCARD [[nodiscard]]
#else
#define METL_NODISCARD
#endif

#if METL_HAS_CPP_ATTRIBUTE(fallthrough) >= 201603L
#define METL_FALLTHROUGH [[fallthrough]]
#else
#define METL_FALLTHROUGH ((void)0)
#endif

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

#if METL_HAS_BUILTIN(__builtin_expect) || METL_COMPILER_GCC || METL_COMPILER_CLANG
#define METL_LIKELY(x) (__builtin_expect(!!(x), 1))
#define METL_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#define METL_LIKELY(x) (!!(x))
#define METL_UNLIKELY(x) (!!(x))
#endif

#if METL_HAS_CPP_ATTRIBUTE(likely) >= 201803L
#define METL_LIKELY_ATTR [[likely]]
#define METL_UNLIKELY_ATTR [[unlikely]]
#else
#define METL_LIKELY_ATTR
#define METL_UNLIKELY_ATTR
#endif

// METL_ISR_SAFE marks functions that are safe to call from interrupt context.
// Requirements (enforced by convention, partially by compiler):
//   - noexcept
//   - no heap allocation
//   - no blocking calls
//   - no virtual dispatch
//   - bounded execution time
// The macro applies noexcept and an always_inline hint for predictable latency.
#define METL_ISR_SAFE noexcept METL_FORCE_INLINE_ATTR

#if METL_COMPILER_MSVC
#define METL_FORCE_INLINE_ATTR
#elif METL_HAS_ATTRIBUTE(always_inline) || METL_COMPILER_GCC || METL_COMPILER_CLANG
#define METL_FORCE_INLINE_ATTR __attribute__((always_inline))
#else
#define METL_FORCE_INLINE_ATTR
#endif

namespace metl {

enum class compiler_id {
  unknown = 0,
  clang = 1,
  gcc = 2,
  msvc = 3,
};

inline constexpr compiler_id active_compiler =
#if METL_COMPILER_CLANG
    compiler_id::clang;
#elif METL_COMPILER_GCC
    compiler_id::gcc;
#elif METL_COMPILER_MSVC
    compiler_id::msvc;
#else
    compiler_id::unknown;
#endif

inline constexpr long cxx_standard = METL_CXX_STANDARD;
inline constexpr bool is_clang = METL_COMPILER_CLANG != 0;
inline constexpr bool is_gcc = METL_COMPILER_GCC != 0;
inline constexpr bool is_msvc = METL_COMPILER_MSVC != 0;
inline constexpr bool has_nodiscard = METL_HAS_CPP_ATTRIBUTE(nodiscard) >= 201603L;
inline constexpr bool has_fallthrough = METL_HAS_CPP_ATTRIBUTE(fallthrough) >= 201603L;

}  // namespace metl
