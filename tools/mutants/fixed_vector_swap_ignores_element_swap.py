"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_vector_swap_ignores_element_swap",
    "file": "include/metl/fixed_vector.hpp",
    "why": "fixed_vector::swap is noexcept although the element swap it calls can "
           "throw, so a throw terminates; fixed_stack and fixed_priority_queue "
           "inherit the claim (#577).",
    "kills": "tool:clang++ -std=c++17 -fsyntax-only -Iinclude -Itests tests/containers/container_swap_test.cpp",
    "old": "                                          std::is_nothrow_move_assignable_v<T> &&\n"
           "                                          std::is_nothrow_swappable_v<T>) {",
    "new": "                                          std::is_nothrow_move_assignable_v<T>) {",
}
