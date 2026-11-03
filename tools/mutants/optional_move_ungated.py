"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "optional_move_ungated",
    "file": "include/metl/optional.hpp",
    "why": "optional declares its move constructor for any T, so "
           "is_move_constructible_v is true for a T that cannot be moved (#601).",
    "kills": "tool:clang++ -std=c++17 -fsyntax-only -Iinclude -Itests tests/containers/copy_move_traits_test.cpp",
    "old": "detail::gated_param<std::is_move_constructible_v<T>, optional&&, 1>",
    "new": "detail::gated_param<true, optional&&, 1>",
}
