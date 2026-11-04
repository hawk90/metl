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

// MSVC leaves __cplusplus at 199711L unless the user passes /Zc:__cplusplus;
// _MSVC_LANG always carries the real standard. Without this, cxx_standard would
// read C++98 on every MSVC build.
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

// __has_cpp_attribute is a preprocessor operator: the standard allows it only in
// #if / #elif. GCC and Clang also accept it in an ordinary expression; MSVC does
// not, and rejected every header through compiler.hpp. So the checks the
// constexpr flags below report are made here, in #if.
#if METL_HAS_CPP_ATTRIBUTE(nodiscard) >= 201603L
#define METL_DETAIL_HAS_NODISCARD 1
#else
#define METL_DETAIL_HAS_NODISCARD 0
#endif
#if METL_HAS_CPP_ATTRIBUTE(fallthrough) >= 201603L
#define METL_DETAIL_HAS_FALLTHROUGH 1
#else
#define METL_DETAIL_HAS_FALLTHROUGH 0
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

// METL_HAVE_INCLUDE(x) — is header `x` includable? Falls back to 0 (assume
// absent) so it may be used in `#if` on toolchains without __has_include.
#if defined(__has_include)
#define METL_HAVE_INCLUDE(x) __has_include(x)
#else
#define METL_HAVE_INCLUDE(x) 0
#endif

// METL_NODISCARD and the rest of the attribute layer live in
// <metl/attributes.hpp>, included at the end of this header so every
// translation unit that includes <metl/compiler.hpp> continues to see them.

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

// C++20 as well as the attribute: clang reports __has_cpp_attribute(likely) in
// C++17 mode too, and then warns under -Wpedantic that it is a C++20 extension.
#if METL_HAS_CPP_ATTRIBUTE(likely) >= 201803L && METL_CXX_STANDARD >= 202002L
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
#define METL_ISR_SAFE noexcept METL_DETAIL_FORCE_INLINE_ATTR

#if METL_COMPILER_MSVC
#define METL_DETAIL_FORCE_INLINE_ATTR
#elif METL_HAS_ATTRIBUTE(always_inline) || METL_COMPILER_GCC || METL_COMPILER_CLANG
#define METL_DETAIL_FORCE_INLINE_ATTR __attribute__((always_inline))
#else
#define METL_DETAIL_FORCE_INLINE_ATTR
#endif
// The old public spelling of an internal helper; removed in 2.0 (#168, #645).
#define METL_FORCE_INLINE_ATTR METL_DETAIL_FORCE_INLINE_ATTR

// Brackets the body of a header whose own entity is deprecated (#645). The
// header names that entity in its own declarations, and GCC warns about each
// such use as soon as the header is included; inside the brackets they stay
// quiet, while a caller's use outside them still warns at the caller's line.
#if defined(_MSC_VER) && !defined(__clang__)
#define METL_DETAIL_DEPRECATED_HEADER_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4996))
#define METL_DETAIL_DEPRECATED_HEADER_END __pragma(warning(pop))
#elif defined(__GNUC__)
#define METL_DETAIL_DEPRECATED_HEADER_BEGIN \
  _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define METL_DETAIL_DEPRECATED_HEADER_END _Pragma("GCC diagnostic pop")
#else
#define METL_DETAIL_DEPRECATED_HEADER_BEGIN
#define METL_DETAIL_DEPRECATED_HEADER_END
#endif

// The variables below are deprecated for removal in 2.0 (#168, #645). Each is
// an inline variable whose initializer differs by compiler or standard, which
// is an ODR violation the moment two translation units disagree. Plain
// [[deprecated]]: <metl/attributes.hpp> is not read yet when this header is
// included first.
METL_DETAIL_DEPRECATED_HEADER_BEGIN

namespace metl {

/// Identifies the compiler that built the translation unit.
enum class [[deprecated(
    "metl::compiler_id is removed in 2.0; test METL_COMPILER_CLANG, _GCC or _MSVC (#645)")]] compiler_id {
  unknown = 0,  ///< Compiler not recognized by the detection macros.
  clang = 1,    ///< LLVM/Clang.
  gcc = 2,      ///< GNU GCC.
  msvc = 3,     ///< Microsoft Visual C++.
};

/// The compiler that built this translation unit (compile-time constant).
[[deprecated("metl::active_compiler is removed in 2.0; test METL_COMPILER_CLANG, _GCC or _MSVC (#645)")]]
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

/// The active language standard value (e.g. 201703L for C++17): `__cplusplus`,
/// or `_MSVC_LANG` on MSVC, whose `__cplusplus` reads 199711L unless
/// `/Zc:__cplusplus` is passed.
[[deprecated("metl::cxx_standard is removed in 2.0; use METL_CXX_STANDARD (#645)")]]
inline constexpr long cxx_standard = METL_CXX_STANDARD;
/// True when the active compiler is Clang.
[[deprecated("metl::is_clang is removed in 2.0; use METL_COMPILER_CLANG (#645)")]]
inline constexpr bool is_clang = METL_COMPILER_CLANG != 0;
/// True when the active compiler is GCC.
[[deprecated("metl::is_gcc is removed in 2.0; use METL_COMPILER_GCC (#645)")]]
inline constexpr bool is_gcc = METL_COMPILER_GCC != 0;
/// True when the active compiler is MSVC.
[[deprecated("metl::is_msvc is removed in 2.0; use METL_COMPILER_MSVC (#645)")]]
inline constexpr bool is_msvc = METL_COMPILER_MSVC != 0;
/// True when the `[[nodiscard]]` attribute is available.
[[deprecated("metl::has_nodiscard is removed in 2.0; use METL_HAS_CPP_ATTRIBUTE(nodiscard) (#645)")]]
inline constexpr bool has_nodiscard = METL_DETAIL_HAS_NODISCARD != 0;
/// True when the `[[fallthrough]]` attribute is available.
[[deprecated("metl::has_fallthrough is removed in 2.0; use METL_HAS_CPP_ATTRIBUTE(fallthrough) (#645)")]]
inline constexpr bool has_fallthrough = METL_DETAIL_HAS_FALLTHROUGH != 0;

}  // namespace metl

METL_DETAIL_DEPRECATED_HEADER_END

// Macros deprecated for removal in 2.0 (#168, #645). A macro cannot carry
// [[deprecated]]; clang 14+ warns at each expansion (-Wdeprecated-pragma).
// GCC and MSVC have no per-macro warning, so their users have the CHANGELOG.
#if defined(__clang__) && defined(__has_warning)
#if __has_warning("-Wdeprecated-pragma")
#pragma clang deprecated(METL_COMPILER_CLANG_VERSION_MAJOR, "removed in 2.0; use __clang_major__ (#645)")
#pragma clang deprecated(METL_COMPILER_CLANG_VERSION_MINOR, "removed in 2.0; use __clang_minor__ (#645)")
#pragma clang deprecated(METL_COMPILER_CLANG_VERSION_PATCH, "removed in 2.0; use __clang_patchlevel__ (#645)")
#pragma clang deprecated(METL_COMPILER_GCC_VERSION_MAJOR, "removed in 2.0; use __GNUC__ (#645)")
#pragma clang deprecated(METL_COMPILER_GCC_VERSION_MINOR, "removed in 2.0; use __GNUC_MINOR__ (#645)")
#pragma clang deprecated(METL_COMPILER_GCC_VERSION_PATCH, "removed in 2.0; use __GNUC_PATCHLEVEL__ (#645)")
#pragma clang deprecated(METL_HAVE_INCLUDE, "removed in 2.0; use __has_include (#645)")
#pragma clang deprecated(METL_FALLTHROUGH, "removed in 2.0; use [[fallthrough]] (#645)")
#pragma clang deprecated(METL_LIKELY, "removed in 2.0; use METL_PREDICT_TRUE (#645)")
#pragma clang deprecated(METL_UNLIKELY, "removed in 2.0; use METL_PREDICT_FALSE (#645)")
#pragma clang deprecated(METL_LIKELY_ATTR, "removed in 2.0; use [[likely]] where the standard has it (#645)")
#pragma clang deprecated(METL_UNLIKELY_ATTR, \
                         "removed in 2.0; use [[unlikely]] where the standard has it (#645)")
#pragma clang deprecated(METL_FORCE_INLINE_ATTR, "internal; removed in 2.0, use METL_FORCE_INLINE (#645)")
#pragma clang deprecated(METL_ISR_SAFE, \
                         "removed in 2.0; it only adds noexcept and always_inline: write noexcept (#645)")
#endif
#endif

// Attribute layer (METL_NODISCARD, METL_LIFETIME_BOUND, METL_CONST_INIT, ...).
// Included last so it can use the detection macros defined above; guarded by
// #pragma once so the mutual include with attributes.hpp is safe.
#include "metl/attributes.hpp"
