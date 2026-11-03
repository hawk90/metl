"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::fixed_function (#639)
MUTANT = {
    "name": "fixed_callable_wraps_empty_delegate",
    "file": "include/metl/fixed_function.hpp",
    "why": "an empty delegate or function_ref converted to a fixed_function is "
           "wrapped as a callable: it reports a target and asserts when called.",
    "kills": "ctest:fixed_function_null",
    "old": """template <typename Signature>
std::true_type is_fixed_callable_test(const delegate<Signature>*);
template <typename Signature>
std::true_type is_fixed_callable_test(const function_ref<Signature>*);
""",
    "new": "",
}
