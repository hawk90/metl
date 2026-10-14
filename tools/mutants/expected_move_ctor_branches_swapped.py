"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#202)
MUTANT = {
    "name": "expected_move_ctor_branches_swapped",
    "file": "include/metl/expected.hpp",
    "why": "the move constructor builds the error from a value and the "
           "value from an error. Copy elision means a prvalue never reaches "
           "it, so only a test that moves a NAMED expected instantiates it.",
    "kills": "ctest:expected_test",
    "old": """    if (holds_value()) {
      construct_value(static_cast<T&&>(other.value_unchecked()));
    } else {
      construct_error(static_cast<E&&>(other.error_unchecked()));
    }""",
    "new": """    if (!holds_value()) {
      construct_value(static_cast<T&&>(other.value_unchecked()));
    } else {
      construct_error(static_cast<E&&>(other.error_unchecked()));
    }""",
}
