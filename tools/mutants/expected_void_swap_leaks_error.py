"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected<void, E> (#365)
MUTANT = {
    "name": "expected_void_swap_leaks_error",
    "file": "include/metl/expected.hpp",
    "why": "swapping success with an error moves the error across but never "
           "destroys the moved-from one: an E that is never destroyed.",
    "kills": "ctest:expected_state_pairs",
    "old": """      other.state_ = detail::expected_state::switching;
      other.error_ptr()->~E();
      other.state_ = detail::expected_state::value;""",
    "new": """      other.state_ = detail::expected_state::switching;
      other.state_ = detail::expected_state::value;""",
}
