"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "optional_copy_never_trivial",
    "file": "include/metl/optional.hpp",
    "why": "optional's copy constructor is user-provided for every T, so "
           "optional<int> is not trivially copyable and returns through a hidden "
           "pointer on AAPCS instead of r0 (#163).",
    "kills": "tool:clang++ -std=c++17 -fsyntax-only -Iinclude -Itests tests/vocab/optional_triviality_test.cpp",
    "old": "template <typename T, bool = std::is_trivially_copy_constructible_v<T>>\nstruct optional_copy :",
    "new": "template <typename T, bool = false>\nstruct optional_copy :",
}
