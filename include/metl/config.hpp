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

// ── Hardening levels ────────────────────────────────────────────────────────
// METL_HARDENING selects how much runtime precondition checking is compiled in,
// modeled on libc++'s _LIBCPP_HARDENING_MODE and Abseil's two-tier asserts.
//
// metl is CHECKED-BY-DEFAULT: ordinary preconditions (bounds, non-empty,
// capacity, allocator overflow) use METL_ASSERT and stay on in release, matching
// the library's "no silent surprises / every precondition asserts by default"
// design. Turning them off is an explicit opt-in, not the default.
//
//   METL_HARDENING_NONE  (0) — no runtime *precondition* checks; both
//        METL_ASSERT and METL_DASSERT are stripped for maximum performance /
//        minimum codegen. The always-on METL_HARDEN security floor still stands.
//        For code proven correct that cannot afford the branch.
//   METL_HARDENING_FAST  (1) — all preconditions (METL_ASSERT) on, including
//        bounds/accessor checks; only the (rare) expensive debug DCHECKs
//        (METL_DASSERT) are stripped. Shipping default — safe, low overhead.
//   METL_HARDENING_DEBUG (2) — everything on, including METL_DASSERT.
//        Development default.
//
// Consumer-controllable via -DMETL_HARDENING=<0|1|2>, independently of the
// consumer's own NDEBUG. If unset it defaults to DEBUG for debug builds
// (METL_DEBUG or !NDEBUG) and FAST for release builds — so debug/release check
// separation applies to consuming code too, not just to metl's own build.
//
// ODR WARNING: METL_HARDENING changes the bodies of inline/template functions in
// these headers (which checks compile in). It MUST be uniform across every
// translation unit linked into a program — mixing levels (e.g. a Debug TU with a
// Release TU) is an ODR violation (UB). This is the same constraint as NDEBUG and
// libc++'s _LIBCPP_HARDENING_MODE; set it once, project-wide.
#define METL_HARDENING_NONE 0
#define METL_HARDENING_FAST 1
#define METL_HARDENING_DEBUG 2

#ifndef METL_HARDENING
#if defined(METL_DEBUG) || !defined(NDEBUG)
#define METL_HARDENING METL_HARDENING_DEBUG
#else
#define METL_HARDENING METL_HARDENING_FAST
#endif
#endif

#if (METL_HARDENING < METL_HARDENING_NONE) || (METL_HARDENING > METL_HARDENING_DEBUG)
#error "METL_HARDENING must be 0 (NONE), 1 (FAST), or 2 (DEBUG)"
#endif

// METL_ASSERT — the default precondition check (bounds, non-empty, capacity,
// allocator overflow, ...). Active at FAST and above, so it stays on in release
// by default; compiled out only at METL_HARDENING_NONE. A failure routes through
// the [[noreturn]] assert path in <metl/assert.hpp>; the failing branch is
// METL_PREDICT_FALSE so the success path stays straight-line. This is the right
// macro for essentially every runtime precondition — reserve METL_DASSERT for
// checks too expensive to keep in a shipping build.
#ifndef METL_ASSERT
#if METL_HARDENING >= METL_HARDENING_FAST
#define METL_ASSERT(expr)                                          \
  do {                                                             \
    if (METL_PREDICT_FALSE(!(expr))) {                             \
      ::metl::detail::assertion_failed(#expr, __FILE__, __LINE__); \
    }                                                              \
  } while (false)
#else
#define METL_ASSERT(expr)         \
  do {                            \
    (void)sizeof((expr) ? 1 : 0); \
  } while (false)
#endif
#endif

// METL_HARDEN — an ALWAYS-ON hard guard, independent of METL_HARDENING (it is
// NOT stripped even at METL_HARDENING_NONE). Reserved for the handful of
// defense-in-depth checks whose failure would let library misuse escalate into a
// wild out-of-bounds *write* / memory-safety hole (e.g. a full-table insert
// reaching an out-of-range index). This is metl's security floor and mirrors
// Abseil's ABSL_HARDENING_ASSERT: preconditions may be compiled out, but the
// "never corrupt arbitrary memory" guarantee is not a precondition and stays.
#ifndef METL_HARDEN
#define METL_HARDEN(expr)                                          \
  do {                                                             \
    if (METL_PREDICT_FALSE(!(expr))) {                             \
      ::metl::detail::assertion_failed(#expr, __FILE__, __LINE__); \
    }                                                              \
  } while (false)
#endif

// METL_DASSERT — debug-only assertion (a DCHECK). Active only at
// METL_HARDENING_DEBUG; at FAST and NONE it compiles to nothing — evaluating
// `expr` only in an unevaluated context so it neither runs side effects nor
// triggers unused warnings.
//
// Use METL_DASSERT ONLY for checks too EXPENSIVE to keep in a shipping build
// (e.g. an O(n) invariant scan, a whole-container consistency sweep). Ordinary
// preconditions — including cheap hot-path bounds/non-empty checks on accessors
// like operator[]/front/back — should use METL_ASSERT so they stay on in release
// (metl is checked-by-default). METL_DASSERT is deliberately rare.
#ifndef METL_DASSERT
#if METL_HARDENING >= METL_HARDENING_DEBUG
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

/// Major version component of the library.
inline constexpr int version_major = 0;
/// Minor version component of the library.
inline constexpr int version_minor = 1;
/// Patch version component of the library.
inline constexpr int version_patch = 0;

}  // namespace metl
