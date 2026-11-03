"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "constant_init_bytes_never_zeroes",
    "file": "include/metl/detail/constant_init.hpp",
    "why": "a raw byte buffer (fixed_function's) is left uninitialized during "
           "constant evaluation, so a namespace-scope fixed_function is no "
           "longer constant-initialized.",
    # constinit_size_test's C++20 build declares a constinit fixed_function.
    "kills": "tool:clang++ -std=c++20 -fsyntax-only -Iinclude -Itests tests/containers/constinit_size_test.cpp",
    "old": """  if (in_constant_evaluation()) {
    for (std::size_t i = 0; i < N; ++i) {""",
    "new": """  if (false) {
    for (std::size_t i = 0; i < N; ++i) {""",
}
