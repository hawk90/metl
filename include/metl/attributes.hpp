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
