"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "invocable_ignores_temporaries",
    "file": "include/metl/detail/invocable.hpp",
    "why": "Whichever form of the check the compiler gets -- either builtin "
           "or the fallback -- its answer is dropped, so a by-value callable "
           "behind a reference-returning signature is accepted again and "
           "returns a reference into its own frame.",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": """struct invoke_binds_temporary : binds_temporary<R, std::invoke_result_t<F, Args...>> {};
""",
    "new": """struct invoke_binds_temporary : std::false_type {};
""",
}
