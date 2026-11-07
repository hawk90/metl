"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "expected_copy_never_trivial",
    "file": "include/metl/expected.hpp",
    "why": "expected's copy constructor is user-provided for every T and E, so "
           "expected<int, int> is not trivially copyable and returns through a "
           "hidden pointer on AAPCS instead of r0 (#163).",
    "kills": "tool:clang++ -std=c++17 -fsyntax-only -Iinclude -Itests tests/vocab/expected_triviality_test.cpp",
    "old": "std::is_trivially_copy_constructible_v<T> && std::is_trivially_copy_constructible_v<E>,",
    "new": "false,",
}
