#pragma once
// Included by a test that exercises a name deprecated for removal in 2.0
// (#645), so the deprecation warnings it would otherwise draw at every use do
// not fail the -Werror builds. Only a test that still exercises a deprecated
// component until #662 removes it includes it (variant, the metl::exp aliases, the compiler.hpp macros and
// variables). Every other test uses the replacement, so a new use of a deprecated name anywhere else still
// fails.
//
// The pragmas stay in force for the rest of the translation unit, which is the
// point: GCC reports a use inside a template where it is instantiated, at the
// end of the file.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(disable : 4996)
#else
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
// The deprecated macros warn under their own flag, on clang 14+ only.
#if defined(__clang__) && defined(__has_warning)
#if __has_warning("-Wdeprecated-pragma")
#pragma clang diagnostic ignored "-Wdeprecated-pragma"
#endif
#endif
