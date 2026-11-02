"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_function_nullptr_not_constant",
    "file": "include/metl/fixed_function.hpp",
    "why": "fixed_function's nullptr constructor leaves the storage uninitialized, so "
           "`constinit fixed_function<void()> f{nullptr}` does not compile (#578).",
    "kills": "tool:clang++ -std=c++17 -fsyntax-only -Iinclude -Itests tests/vocab/vocab_constant_init_test.cpp",
    "old": "  constexpr fixed_function_impl(std::nullptr_t) noexcept : fixed_function_impl() {}",
    "new": "  constexpr fixed_function_impl(std::nullptr_t) noexcept : ops_(nullptr) {}",
}
