#pragma once
// Included by a test that exercises a name deprecated for removal in 2.0
// (#645), so the deprecation warnings it would otherwise draw at every use do
// not fail the -Werror builds. Only two kinds of test include it:
// tests/core/deprecated_names_test.cpp, and a test that still exercises a
// deprecated component until 2.0 removes it (static_allocator, variant,
// static_message_queue). Every other test uses the replacement, so a new use
// of a deprecated name anywhere else still fails.
//
// The pragmas stay in force for the rest of the translation unit, which is the
// point: GCC reports a use inside a template where it is instantiated, at the
// end of the file.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(disable : 4996)
#else
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
