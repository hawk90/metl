"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#202)
MUTANT = {
    "name": "expected_swap_value_error_leaks_error",
    "file": "include/metl/expected.hpp",
    "why": "the value<->error swap (E nothrow-movable) never destroys the "
           "old error. Both sides read right afterwards; one E is leaked, "
           "which only a lifetime count sees.",
    "kills": "ctest:expected_test",
    "old": """      E tmp(static_cast<E&&>(*e.error_ptr()));
      e.error_ptr()->~E();""",
    "new": """      E tmp(static_cast<E&&>(*e.error_ptr()));""",
}
