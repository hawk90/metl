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

// METL_NODISCARD — warn when a returned value is discarded.
#ifndef METL_NODISCARD
#if METL_HAS_CPP_ATTRIBUTE(nodiscard) >= 201603L
#define METL_NODISCARD [[nodiscard]]
#else
#define METL_NODISCARD
#endif
#endif

// METL_NORETURN — the function never returns to its caller.
#ifndef METL_NORETURN
#if METL_HAS_CPP_ATTRIBUTE(noreturn) >= 200809L
#define METL_NORETURN [[noreturn]]
#else
#define METL_NORETURN
#endif
#endif

// METL_ALWAYS_INLINE — force inlining regardless of the optimizer's heuristics.
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

// METL_MAYBE_UNUSED — suppress unused-entity warnings for a declaration.
#ifndef METL_MAYBE_UNUSED
#if METL_HAS_CPP_ATTRIBUTE(maybe_unused) >= 201603L
#define METL_MAYBE_UNUSED [[maybe_unused]]
#else
#define METL_MAYBE_UNUSED
#endif
#endif

// METL_DEPRECATED(msg) — mark an entity deprecated with an explanatory message.
#ifndef METL_DEPRECATED
#if METL_HAS_CPP_ATTRIBUTE(deprecated) >= 201309L
#define METL_DEPRECATED(msg) [[deprecated(msg)]]
#else
#define METL_DEPRECATED(msg)
#endif
#endif

// METL_LIFETIME_BOUND — flags a reference/pointer parameter (or implicit object
// parameter) whose referent must outlive the returned/stored result, so the
// compiler can diagnose obvious dangling at the call site (clang -Wdangling).
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

// METL_CONST_INIT — require that a static/thread-storage variable is
// constant-initialized (no dynamic init, immune to static-init-order fiasco).
// Prefers the C++20 `constinit` keyword when the toolchain provides it,
// otherwise the clang attribute, otherwise empty.
#ifndef METL_CONST_INIT
#if defined(__cpp_constinit) && __cpp_constinit >= 201907L
#define METL_CONST_INIT constinit
#elif METL_HAS_CPP_ATTRIBUTE(clang::require_constant_initialization)
#define METL_CONST_INIT [[clang::require_constant_initialization]]
#else
#define METL_CONST_INIT
#endif
#endif

// METL_ATTRIBUTE_TRIVIAL_ABI — allow a type with a non-trivial special member
// to still be passed/returned in registers and destroyed by the callee, when
// the type is otherwise trivially relocatable. Improves codegen for small
// owning wrappers (e.g. smart pointers). No-op where unavailable.
#ifndef METL_ATTRIBUTE_TRIVIAL_ABI
#if METL_HAS_CPP_ATTRIBUTE(clang::trivial_abi)
#define METL_ATTRIBUTE_TRIVIAL_ABI [[clang::trivial_abi]]
#elif METL_HAS_ATTRIBUTE(trivial_abi)
#define METL_ATTRIBUTE_TRIVIAL_ABI __attribute__((trivial_abi))
#else
#define METL_ATTRIBUTE_TRIVIAL_ABI
#endif
#endif
