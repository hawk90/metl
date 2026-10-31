"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_queue_size_not_constexpr",
    "file": "include/metl/fixed_queue.hpp",
    "why": "a query stops being usable during constant evaluation, so the "
           "sequence containers disagree again and a constant-initialized "
           "object that asks for its size no longer compiles.",
    "kills": "tool:clang++ -std=c++20 -fsyntax-only -Iinclude -Itests tests/containers/sequence_queries_test.cpp",
    "old": "METL_NODISCARD constexpr size_type size() const noexcept { return storage_.size(); }",
    "new": "METL_NODISCARD size_type size() const noexcept { return storage_.size(); }",
}
