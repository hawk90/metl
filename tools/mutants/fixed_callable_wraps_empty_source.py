"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_callable_wraps_empty_source",
    "file": "include/metl/fixed_function.hpp",
    "why": "an empty fixed_function converted to another capacity is "
           "wrapped as a callable: it reports a target and asserts when "
           "called.",
    "kills": "ctest:fixed_function_null",
    "old": """inline constexpr bool is_fixed_callable_v =
    decltype(is_fixed_callable_test(static_cast<const F*>(nullptr)))::value;""",
    "new": """inline constexpr bool is_fixed_callable_v = false;""",
}
