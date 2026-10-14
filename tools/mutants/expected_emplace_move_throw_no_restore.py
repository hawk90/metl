"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#202)
MUTANT = {
    "name": "expected_emplace_move_throw_no_restore",
    "file": "include/metl/expected.hpp",
    "why": "when the move that commits a new value throws, the backed-up "
           "error is not put back: a destroyed E under has_value() == false.",
    "kills": "ctest:expected_regression",
    "old": """        construct_error(static_cast<E&&>(backup));
        state_ = detail::expected_state::error;
        throw;""",
    "new": """        state_ = detail::expected_state::error;
        throw;""",
}
