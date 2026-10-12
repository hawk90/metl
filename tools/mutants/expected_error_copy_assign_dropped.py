"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#202)
MUTANT = {
    "name": "expected_error_copy_assign_dropped",
    "file": "include/metl/expected.hpp",
    "why": "assigning an error to an expected that already holds one keeps "
           "the OLD error. The state is right, so has_value() checks pass.",
    "kills": "ctest:expected_test",
    "old": """      *error_ptr() = std::forward<G>(error);
      return;""",
    "new": """      return;""",
}
