#pragma once

// Portable attribute layer (abseil `absl/base/attributes.h` style).
//
// Every macro is gated on `__has_cpp_attribute` / `__has_attribute` (via the
// detection macros in <metl/compiler.hpp>) and degrades to an empty expansion
// on compilers that do not support it. Applying any of these macros is
// therefore always safe: it is either honored or a no-op, never an error.
//
// This header consolidates METL_NODISCARD (historically defined in
// compiler.hpp) together with the rest of the attribute macros. compiler.hpp
// includes this header at its end so any translation unit that includes
// <metl/compiler.hpp> continues to see METL_NODISCARD unchanged.

#include "metl/compiler.hpp"

/// @brief Maps to `[[nodiscard]]` where supported: warn when a return value is
///        discarded. Empty on compilers without the attribute.
#ifndef METL_NODISCARD
#if METL_HAS_CPP_ATTRIBUTE(nodiscard) >= 201603L
#define METL_NODISCARD [[nodiscard]]
#else
#define METL_NODISCARD
#endif
#endif

/// @brief Maps to `[[noreturn]]`: the function never returns to its caller.
#ifndef METL_NORETURN
#if METL_HAS_CPP_ATTRIBUTE(noreturn) >= 200809L
#define METL_NORETURN [[noreturn]]
#else
#define METL_NORETURN
#endif
#endif

/// @brief Force inlining regardless of optimizer heuristics. Maps to
///        `__forceinline` (MSVC) or `inline __attribute__((always_inline))`.
// This is a stronger request than METL_FORCE_INLINE's hint and mirrors the
// abseil ABSL_ATTRIBUTE_ALWAYS_INLINE spelling.
#ifndef METL_ALWAYS_INLINE
#if METL_COMPILER_MSVC
#define METL_ALWAYS_INLINE __forceinline
#elif METL_HAS_ATTRIBUTE(always_inline) || METL_COMPILER_GCC || METL_COMPILER_CLANG
#define METL_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define METL_ALWAYS_INLINE inline
#endif
#endif

/// @brief Maps to `[[maybe_unused]]`: suppress unused-entity warnings.
#ifndef METL_MAYBE_UNUSED
#if METL_HAS_CPP_ATTRIBUTE(maybe_unused) >= 201603L
#define METL_MAYBE_UNUSED [[maybe_unused]]
#else
#define METL_MAYBE_UNUSED
#endif
#endif

/// @brief Maps to `[[deprecated(msg)]]`: mark an entity deprecated with a message.
#ifndef METL_DEPRECATED
#if METL_HAS_CPP_ATTRIBUTE(deprecated) >= 201309L
#define METL_DEPRECATED(msg) [[deprecated(msg)]]
#else
#define METL_DEPRECATED(msg)
#endif
#endif

/// @brief Maps to `[[clang::lifetimebound]]`: flags a reference/pointer parameter
///        whose referent must outlive the result, enabling dangling diagnostics.
// Empty everywhere the attribute is unavailable, so it never breaks a build.
#ifndef METL_LIFETIME_BOUND
#if METL_HAS_CPP_ATTRIBUTE(clang::lifetimebound)
#define METL_LIFETIME_BOUND [[clang::lifetimebound]]
#elif METL_HAS_ATTRIBUTE(lifetimebound)
#define METL_LIFETIME_BOUND __attribute__((lifetimebound))
#else
#define METL_LIFETIME_BOUND
#endif
#endif

/// @brief Require constant initialization of a static/thread-storage variable
///        (immune to static-init-order fiasco). Maps to `constinit` or the
///        clang `require_constant_initialization` attribute.
#ifndef METL_CONST_INIT
#if defined(__cpp_constinit) && __cpp_constinit >= 201907L
#define METL_CONST_INIT constinit
#elif METL_HAS_CPP_ATTRIBUTE(clang::require_constant_initialization)
#define METL_CONST_INIT [[clang::require_constant_initialization]]
#else
#define METL_CONST_INIT
#endif
#endif

/// @brief Maps to `[[clang::trivial_abi]]`: let a type with a non-trivial
///        special member still pass/return in registers. No-op where unavailable.
#ifndef METL_ATTRIBUTE_TRIVIAL_ABI
#if METL_HAS_CPP_ATTRIBUTE(clang::trivial_abi)
#define METL_ATTRIBUTE_TRIVIAL_ABI [[clang::trivial_abi]]
#elif METL_HAS_ATTRIBUTE(trivial_abi)
#define METL_ATTRIBUTE_TRIVIAL_ABI __attribute__((trivial_abi))
#else
#define METL_ATTRIBUTE_TRIVIAL_ABI
#endif
#endif
