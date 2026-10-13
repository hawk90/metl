"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected<void, E> (#365)
MUTANT = {
    "name": "expected_void_swap_leaks_error",
    "file": "include/metl/expected.hpp",
    "why": "swapping success with an error moves the error across but never "
           "destroys the moved-from one: an E that is never destroyed.",
    "kills": "ctest:expected_state_pairs",
    "old": """      construct_error(static_cast<E&&>(*other.error_ptr()));
      other.error_ptr()->~E();
      has_value_ = false;
      other.has_value_ = true;""",
    "new": """      construct_error(static_cast<E&&>(*other.error_ptr()));
      has_value_ = false;
      other.has_value_ = true;""",
}
