#pragma once

#include "metl/assert.hpp"
#include "metl/compiler.hpp"
#include "metl/detail/abi.hpp"
#include "metl/optimization.hpp"

// METL_NO_EXCEPTIONS (detected from the compiler's exception mode), the
// hardening levels, their default and the version macros are defined in
// <metl/detail/abi.hpp>, because the ABI tag encodes them.

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
//        (METL_DASSERT) are stripped. The default — safe, low overhead.
//   METL_HARDENING_DEBUG (2) — everything on, including METL_DASSERT. Opt in
//        with -DMETL_HARDENING=2 for development builds.
//
// Consumer-controllable via -DMETL_HARDENING=<0|1|2>. If unset it is FAST,
// whatever NDEBUG says, as libc++'s hardening mode ignores NDEBUG: a level
// that followed NDEBUG would differ between a Debug application and a Release
// SDK built from the same headers, which is the ODR violation described below.
//
// ODR: METL_HARDENING and METL_NO_EXCEPTIONS change the bodies of inline and
// template functions (which checks compile in; 35 sites differ with and without
// exceptions). Since 2.0 the ABI tag (<metl/detail/abi.hpp>, #344) puts each
// configuration's code under its own namespace, so translation units built at
// different levels keep their own checks, and a function that passes a METL
// type between two of them fails to link instead of running the other's code.
// Set the level once, project-wide, all the same: a program that links two
// configurations carries two copies of what both use.
//
// The C++ standard is part of the same constraint. Every translation unit must
// be compiled with the same -std: under C++20 some headers change the bodies of
// inline functions (METL_DETAIL_CONSTEXPR20, METL_DETAIL_LAZY_CONSTANT_INIT),
// and arena_allocator and monotonic_buffer change the type of their storage
// member. A C++17 TU and a C++20 TU that share one of those types define it
// twice, an ODR violation that nothing detects at link time.
// ---------------------------------------------------------------------------
// METL_CRC_TABLE — how the CRC-8/16/32 helpers trade flash for speed.
//
//   0  bit-at-a-time: eight shift-and-conditional-xor steps per byte, zero
//      table.
//   1  nibble table (default): two lookups per byte from a 16-entry table, so
//      16 bytes of flash for CRC-8, 32 for CRC-16, 64 for CRC-32 — and only for
//      the widths a program actually uses.
//
// The default is 1 because the bit-at-a-time loop measures ~8 ns per byte,
// which is a poor trade for the tens of bytes the table costs. Set
// -DMETL_CRC_TABLE=0 to select the table-free loop on a genuinely
// flash-starved target; the results are byte-for-byte identical either way, and
// a test pins that.
//
// ODR: like METL_HARDENING, this changes the bodies of inline functions and must
// be uniform across every TU in a program.
// The message every container and vocabulary type gives for a T it cannot hold,
// as the first statement of the class: without it, T = int&, void, void() or
// int[3] stopped the build with errors from deep inside the header.
#define METL_DETAIL_OBJECT_TYPE_MESSAGE \
  "a METL container or vocabulary type holds objects: T must not be a reference, function, array or void"

#ifndef METL_CRC_TABLE
#define METL_CRC_TABLE 1
#endif

// METL_ASSERT_STRINGS — whether a failed check carries its text.
//
// 1 (default): METL_ASSERT and METL_HARDEN pass the handler the expression and
// the file name, and METL_PANIC passes the file name. Each one is kept in flash.
// 0: each of those is an empty string, never null, and the line number stays, so
// a handler that prints them keeps working. To find the failed check, record the
// return address in the handler and look it up in the image's symbols.
//
// ODR: like METL_HARDENING, this changes the bodies of inline functions and must
// be uniform across every TU in a program.
#ifndef METL_ASSERT_STRINGS
#define METL_ASSERT_STRINGS 1
#endif

#if METL_ASSERT_STRINGS
#define METL_DETAIL_CHECK_EXPRESSION(expr) #expr
#define METL_DETAIL_CHECK_FILE __FILE__
#else
#define METL_DETAIL_CHECK_EXPRESSION(expr) ""
#define METL_DETAIL_CHECK_FILE ""
#endif

// METL_ASSERT — the default precondition check (bounds, non-empty, capacity,
// allocator overflow, ...). Active at FAST and above, so it stays on in release
// by default; compiled out only at METL_HARDENING_NONE. A failure routes through
// the [[noreturn]] assert path in <metl/assert.hpp>; the failing branch is
// METL_PREDICT_FALSE so the success path stays straight-line. This is the right
// macro for essentially every runtime precondition — reserve METL_DASSERT for
// checks too expensive to keep in a shipping build.
#ifdef METL_ASSERT
#error \
    "METL_ASSERT must not be predefined: it is METL's precondition check. Use METL_HARDENING to choose the level and metl::set_assert_handler to act on a failure"
#endif
#if METL_HARDENING >= METL_HARDENING_FAST
#define METL_ASSERT(expr)                                                        \
  do {                                                                           \
    if (METL_PREDICT_FALSE(!(expr))) {                                           \
      ::metl::detail::assertion_failed(                                          \
          METL_DETAIL_CHECK_EXPRESSION(expr), METL_DETAIL_CHECK_FILE, __LINE__); \
    }                                                                            \
  } while (false)
#else
// Type-checks `expr` without running it. Not `sizeof(expr)`: that is an
// unevaluated operand, where C++17 forbids a lambda, so an assertion that
// compiled in a checked build failed to compile in the stripped one.
#define METL_ASSERT(expr) (false ? static_cast<void>(!(expr)) : static_cast<void>(0))
#endif

// METL_HARDEN — an ALWAYS-ON guard, independent of METL_HARDENING (it is NOT
// stripped at METL_HARDENING_NONE). This is metl's security floor, as Abseil's
// ABSL_HARDENING_ASSERT is: preconditions may be compiled out, but these may not.
//
// The rule for which one a check is. METL_HARDEN when, with the check gone, the
// LIBRARY ITSELF would:
//   1. write out of bounds, or corrupt its own size, counters or state;
//   2. loop without bound (a resize that can never finish);
//   3. hand out or store a hazard that fails somewhere else later -- a span
//      whose size() is wrong, a null callable stored as engaged, a parse result
//      built from an error;
//   4. call through an empty delegate / fixed_function / function_ref (a jump
//      to address 0, which on Cortex-M is the vector table);
//   5. form a misaligned mmio or atomic_ref pointer.
// METL_ASSERT for the CALLER's side of an access -- operator[], front()/back(),
// value(), get<>() -- which at NONE behaves like the unchecked std accessor it
// mirrors, and for asserting forms whose stripped result stays in bounds.
// tools/check_source_rules.py (S2) holds the line between the two.
#ifdef METL_HARDEN
#error \
    "METL_HARDEN must not be predefined: it is METL's always-on security floor. Use metl::set_assert_handler to act on a failure"
#endif
#define METL_HARDEN(expr)                                                        \
  do {                                                                           \
    if (METL_PREDICT_FALSE(!(expr))) {                                           \
      ::metl::detail::assertion_failed(                                          \
          METL_DETAIL_CHECK_EXPRESSION(expr), METL_DETAIL_CHECK_FILE, __LINE__); \
    }                                                                            \
  } while (false)

// METL_DASSERT — debug-only assertion (a DCHECK). Active only at
// METL_HARDENING_DEBUG; at FAST and NONE it compiles to nothing — `expr` is
// type-checked in a branch that never runs, so it neither runs side effects nor
// triggers unused warnings.
//
// Use METL_DASSERT ONLY for checks too EXPENSIVE to keep in a shipping build
// (e.g. an O(n) invariant scan, a whole-container consistency sweep). Ordinary
// preconditions — including cheap hot-path bounds/non-empty checks on accessors
// like operator[]/front/back — should use METL_ASSERT so they stay on in release
// (metl is checked-by-default). METL_DASSERT is deliberately rare.
#ifdef METL_DASSERT
#error \
    "METL_DASSERT must not be predefined: it is METL's debug check. Use METL_HARDENING to choose the level and metl::set_assert_handler to act on a failure"
#endif
#if METL_HARDENING >= METL_HARDENING_DEBUG
#define METL_DASSERT(expr) METL_ASSERT(expr)
#else
// As the stripped METL_ASSERT: `expr` is checked, never run.
#define METL_DASSERT(expr) (false ? static_cast<void>(!(expr)) : static_cast<void>(0))
#endif

#ifndef METL_PANIC
#define METL_PANIC(message)                                     \
  do {                                                          \
    ::metl::panic((message), METL_DETAIL_CHECK_FILE, __LINE__); \
  } while (false)
#endif

// The library version: METL_VERSION_MAJOR, _MINOR and _PATCH are defined in
// <metl/detail/abi.hpp>, where a release changes them, and the constants below
// carry the same values for C++ code.
namespace metl {
METL_DETAIL_ABI_BEGIN

/// Major version component of the library, as `METL_VERSION_MAJOR`.
inline constexpr int version_major = METL_VERSION_MAJOR;
/// Minor version component of the library, as `METL_VERSION_MINOR`.
inline constexpr int version_minor = METL_VERSION_MINOR;
/// Patch version component of the library, as `METL_VERSION_PATCH`.
inline constexpr int version_patch = METL_VERSION_PATCH;

METL_DETAIL_ABI_END
}  // namespace metl
