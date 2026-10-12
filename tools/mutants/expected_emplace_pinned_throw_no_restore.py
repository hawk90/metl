"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#202)
MUTANT = {
    "name": "expected_emplace_pinned_throw_no_restore",
    "file": "include/metl/expected.hpp",
    "why": "the same for a non-movable T whose constructor throws: the "
           "previous error is destroyed and never restored.",
    "kills": "ctest:expected_regression",
    "old": """        construct_error(static_cast<E&&>(backup));  // restore the previous state""",
    "new": """        // restore the previous state""",
}
