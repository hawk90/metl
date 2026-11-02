"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "optional_swap_ignores_value_swap",
    "file": "include/metl/optional.hpp",
    "why": "optional::swap is noexcept although the value swap it calls can throw, "
           "so a throw terminates (#600).",
    "kills": "tool:clang++ -std=c++17 -fsyntax-only -Iinclude -Itests tests/vocab/vocab_std_parity_test.cpp",
    "old": "std::is_nothrow_move_assignable_v<T> &&\n                                      std::is_nothrow_swappable_v<T>) {",
    "new": "std::is_nothrow_move_assignable_v<T>) {",
}
