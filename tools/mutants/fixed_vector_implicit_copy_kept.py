"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_vector_implicit_copy_kept",
    "file": "include/metl/fixed_vector.hpp",
    "why": "Without the base that deletes them, a gated-out copy or move is replaced "
           "by the implicit one, which copies the raw storage bytes of a "
           "non-copyable element (#601).",
    "kills": "tool:clang++ -std=c++17 -fsyntax-only -Iinclude -Itests tests/containers/copy_move_traits_test.cpp",
    "old": "class fixed_vector : detail::no_implicit_copy_move<fixed_vector<T, Capacity>> {",
    "new": "class fixed_vector {",
}
