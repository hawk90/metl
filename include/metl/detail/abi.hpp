#pragma once

// The ABI namespace (#344), and the settings it encodes.
//
// Every METL declaration lives in
//
//     metl::v<major>::h<hardening>_noexc<no-exceptions>_tabi<trivial-abi>
//
// through two inline namespaces, so code still names `metl::fixed_vector`. The
// names exist for the linker. A setting that changes the code of an inline
// function changes the name of every function METL defines, so:
//
// - Two translation units built with different settings, each using METL only
//   inside itself, keep their own code. Before 2.0 the linker kept one copy of
//   each inline function, chosen by link order, and a FAST TU could run the
//   NONE copy of operator[] (tools/check_abi_tag.py reproduces it).
// - A function that takes or returns a METL type across them, such as a
//   prebuilt SDK built at another level, fails to link, and the undefined
//   symbol names both configurations.
//
// What the tag encodes, and what it leaves out:
//
//   METL_HARDENING                  which checks compile in
//   METL_NO_EXCEPTIONS              35 sites differ with and without exceptions
//   METL_INTRUSIVE_PTR_TRIVIAL_ABI  its effective value: on only where the
//                                   compiler honours [[clang::trivial_abi]],
//                                   so a GCC and a Clang object that both set
//                                   it still disagree, as their calling
//                                   conventions do
//
//   METL_CRC_TABLE, METL_ASSERT_STRINGS   not tagged: either value gives the
//                                         same results; only the table or the
//                                         text of a failed check differs
//   METL_CACHELINE_SIZE                   not a setting: fixed per target
//   the C++ standard (-std)               not tagged: every TU must use the
//                                         same one (config.hpp)
//
// The assert and panic handler slots sit in `metl::v<major>` only, outside the
// configuration's namespace, so metl::set_assert_handler reaches a check that
// fails under any configuration in the program.
//
// The pattern is Abseil's ABSL_NAMESPACE_BEGIN / ABSL_NAMESPACE_END
// (absl/base/config.h): each header keeps its `namespace metl {` and opens the
// inline namespaces just inside it.
//
// This header holds only macros and includes only <metl/compiler.hpp>, so every
// header can include it first. The settings it reads are defined here, before
// any header can see them.

#include "metl/compiler.hpp"

// ── Version ────────────────────────────────────────────────────────────────
// The library version, written once here. The macros are for the
// preprocessor (`#if METL_VERSION_MAJOR >= 2`); <metl/config.hpp> defines
// metl::version_major and the rest from them. A release changes the three
// numbers on these lines.
#define METL_VERSION_MAJOR 2
#define METL_VERSION_MINOR 0
#define METL_VERSION_PATCH 0

// ── Exceptions ─────────────────────────────────────────────────────────────
#ifndef METL_NO_EXCEPTIONS
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define METL_NO_EXCEPTIONS 0
#else
#define METL_NO_EXCEPTIONS 1
#endif
#endif
#if METL_NO_EXCEPTIONS != 0 && METL_NO_EXCEPTIONS != 1
#error "METL_NO_EXCEPTIONS must be 0 or 1"
#endif

// ── Hardening level ────────────────────────────────────────────────────────
// What each level checks is described in <metl/config.hpp>.
#define METL_HARDENING_NONE 0
#define METL_HARDENING_FAST 1
#define METL_HARDENING_DEBUG 2

#ifndef METL_HARDENING
#define METL_HARDENING METL_HARDENING_FAST
#endif

// Validated by token pasting, not by range. The preprocessor reads an unknown
// identifier as 0, so a range check let -DMETL_HARDENING=DEBUG (the level's
// name, not its macro) through as NONE -- every precondition check gone, and no
// diagnostic. Pasting accepts exactly 0, 1 and 2, after METL_HARDENING_* has
// expanded to one of them.
#define METL_DETAIL_HARDENING_VALID_0 1
#define METL_DETAIL_HARDENING_VALID_1 1
#define METL_DETAIL_HARDENING_VALID_2 1
#define METL_DETAIL_HARDENING_PASTE(level) METL_DETAIL_HARDENING_VALID_##level
#define METL_DETAIL_HARDENING_VALID(level) METL_DETAIL_HARDENING_PASTE(level)
#if METL_DETAIL_HARDENING_VALID(METL_HARDENING) != 1
#error "METL_HARDENING must be 0 (NONE), 1 (FAST), or 2 (DEBUG), or METL_HARDENING_NONE / _FAST / _DEBUG"
#endif

// ── intrusive_ptr calling convention ───────────────────────────────────────
// See <metl/intrusive_ptr.hpp>. Off by default; the tag records whether it is
// in effect, which needs the compiler to honour the attribute.
#ifndef METL_INTRUSIVE_PTR_TRIVIAL_ABI
#define METL_INTRUSIVE_PTR_TRIVIAL_ABI 0
#endif
#if METL_INTRUSIVE_PTR_TRIVIAL_ABI && \
    (METL_HAS_CPP_ATTRIBUTE(clang::trivial_abi) || METL_HAS_ATTRIBUTE(trivial_abi))
#define METL_DETAIL_INTRUSIVE_PTR_TRIVIAL_ABI_EFFECTIVE 1
#else
#define METL_DETAIL_INTRUSIVE_PTR_TRIVIAL_ABI_EFFECTIVE 0
#endif

// ── The namespaces ─────────────────────────────────────────────────────────
// Each argument is expanded before it is pasted, so METL_HARDENING_DEBUG
// becomes 2 first.
#define METL_DETAIL_ABI_PASTE6(a, b, c, d, e, f) a##b##c##d##e##f
#define METL_DETAIL_ABI_JOIN6(a, b, c, d, e, f) METL_DETAIL_ABI_PASTE6(a, b, c, d, e, f)
#define METL_DETAIL_ABI_PASTE2(a, b) a##b
#define METL_DETAIL_ABI_JOIN2(a, b) METL_DETAIL_ABI_PASTE2(a, b)

#define METL_DETAIL_ABI_VERSION METL_DETAIL_ABI_JOIN2(v, METL_VERSION_MAJOR)
#define METL_DETAIL_ABI_CONFIG \
  METL_DETAIL_ABI_JOIN6(       \
      h, METL_HARDENING, _noexc, METL_NO_EXCEPTIONS, _tabi, METL_DETAIL_INTRUSIVE_PTR_TRIVIAL_ABI_EFFECTIVE)

// Used just inside `namespace metl {` and just before its closing brace.
#define METL_DETAIL_ABI_VERSION_BEGIN inline namespace METL_DETAIL_ABI_VERSION {
#define METL_DETAIL_ABI_VERSION_END }
#define METL_DETAIL_ABI_BEGIN METL_DETAIL_ABI_VERSION_BEGIN inline namespace METL_DETAIL_ABI_CONFIG {
#define METL_DETAIL_ABI_END \
  }                         \
  METL_DETAIL_ABI_VERSION_END
