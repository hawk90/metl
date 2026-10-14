"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#202)
MUTANT = {
    "name": "expected_error_error_swap_dropped",
    "file": "include/metl/expected.hpp",
    "why": "swapping two expecteds that both hold errors does nothing.",
    "kills": "ctest:expected_test",
    "old": """      swap(*value_ptr(), *other.value_ptr());
    } else if (!holds_value() && !other.holds_value()) {
      using std::swap;
      swap(*error_ptr(), *other.error_ptr());
    } else if (holds_value() && !other.holds_value()) {""",
    "new": """      swap(*value_ptr(), *other.value_ptr());
    } else if (!holds_value() && !other.holds_value()) {
    } else if (holds_value() && !other.holds_value()) {""",
}
