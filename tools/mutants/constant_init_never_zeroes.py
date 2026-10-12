"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "constant_init_never_zeroes",
    "file": "include/metl/detail/constant_init.hpp",
    "why": "the containers' storage is left uninitialized during constant "
           "evaluation, so no namespace-scope container is constant-"
           "initialized and an early interrupt's pushes are wiped.",
    # The C++20 build of init_order_test is the gate: its constinit
    # declarations stop compiling.
    "kills": "tool:clang++ -std=c++20 -fsyntax-only -Iinclude -Itests tests/sync/init_order_queues.cpp",
    "old": """  if (in_constant_evaluation()) {
    for (std::size_t i = 0; i < N; ++i) {""",
    "new": """  if (false) {
    for (std::size_t i = 0; i < N; ++i) {""",
}
