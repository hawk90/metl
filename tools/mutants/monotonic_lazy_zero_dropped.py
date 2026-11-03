"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "monotonic_lazy_zero_dropped",
    "file": "include/metl/monotonic_buffer.hpp",
    "why": "under C++20 the constructor no longer zeroes the buffer during "
           "constant evaluation, so a namespace-scope monotonic_buffer is not "
           "constant-initialized and an early interrupt's allocations are wiped (#628).",
    # constinit_size_test's C++20 build declares a constinit monotonic_buffer.
    "kills": "tool:clang++ -std=c++20 -fsyntax-only -Iinclude -Itests tests/containers/constinit_size_test.cpp",
    "old": """  constexpr monotonic_buffer() noexcept : offset_(0) { detail::zero_for_constant_init(storage_); }""",
    "new": """  constexpr monotonic_buffer() noexcept : offset_(0) {}""",
}
