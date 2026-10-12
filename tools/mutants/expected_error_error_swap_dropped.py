"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#202)
MUTANT = {
    "name": "expected_error_error_swap_dropped",
    "file": "include/metl/expected.hpp",
    "why": "swapping two expecteds that both hold errors does nothing.",
    "kills": "ctest:expected_test",
    "old": """      swap(*value_ptr(), *other.value_ptr());
    } else if (!has_value_ && !other.has_value_) {
      using std::swap;
      swap(*error_ptr(), *other.error_ptr());
    } else if (has_value_ && !other.has_value_) {""",
    "new": """      swap(*value_ptr(), *other.value_ptr());
    } else if (!has_value_ && !other.has_value_) {
    } else if (has_value_ && !other.has_value_) {""",
}
