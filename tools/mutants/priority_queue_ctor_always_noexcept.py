"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "priority_queue_ctor_always_noexcept",
    "file": "include/metl/fixed_priority_queue.hpp",
    "why": "the default constructor reports nothrow for a throwing comparator "
           "and terminates when that comparator's constructor throws.",
    "kills": "tool:clang++ -std=c++17 -fsyntax-only -Iinclude -Itests tests/containers/sequence_queries_test.cpp",
    "old": "constexpr fixed_priority_queue() noexcept(std::is_nothrow_default_constructible_v<Compare>) = default;",
    "new": "constexpr fixed_priority_queue() noexcept = default;",
}
