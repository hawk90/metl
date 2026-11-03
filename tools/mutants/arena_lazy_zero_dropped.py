"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "arena_lazy_zero_dropped",
    "file": "include/metl/arena_allocator.hpp",
    "why": "under C++20 the constructor no longer zeroes the buffer during "
           "constant evaluation, so a namespace-scope arena_allocator is not "
           "constant-initialized and an early interrupt's allocations are wiped (#628).",
    # constinit_size_test's C++20 build declares a constinit arena_allocator.
    "kills": "tool:clang++ -std=c++20 -fsyntax-only -Iinclude -Itests tests/containers/constinit_size_test.cpp",
    "old": """  constexpr arena_allocator() noexcept : offset_(0) { detail::zero_for_constant_init(storage_); }""",
    "new": """  constexpr arena_allocator() noexcept : offset_(0) {}""",
}
