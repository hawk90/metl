"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#202)
MUTANT = {
    "name": "expected_swap_value_error_leaks_error_throwing_move",
    "file": "include/metl/expected.hpp",
    "why": "the same leak on the other branch, taken when only T is "
           "nothrow-movable: the old error is overwritten, not destroyed.",
    "kills": "ctest:expected_test",
    "old": """      e.error_ptr()->~E();
      ::new (e.storage_addr()) T(static_cast<T&&>(tmp));""",
    "new": """      ::new (e.storage_addr()) T(static_cast<T&&>(tmp));""",
}
