"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_vector_copy_ungated",
    "file": "include/metl/fixed_vector.hpp",
    "why": "fixed_vector declares its copy constructor for any element, so "
           "is_copy_constructible_v is true for an element that cannot be copied (#601).",
    "kills": "tool:clang++ -std=c++17 -fsyntax-only -Iinclude -Itests tests/containers/copy_move_traits_test.cpp",
    "old": "fixed_vector(detail::gated_param<std::is_copy_constructible_v<T>, const fixed_vector&, 0> other)",
    "new": "fixed_vector(detail::gated_param<true, const fixed_vector&, 0> other)",
}
