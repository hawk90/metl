"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_vector_try_assign_range_rejects_exact",
    "file": "include/metl/fixed_vector.hpp",
    "why": "try_assign(first, last) refuses a range of exactly Capacity.",
    "kills": "ctest:fixed_vector_test",
    "old": """    if (static_cast<size_type>(detail::iterator_distance(first, last)) > Capacity) {
      return false;
    }
    assign(first, last);""",
    "new": """    if (static_cast<size_type>(detail::iterator_distance(first, last)) >= Capacity) {
      return false;
    }
    assign(first, last);""",
}
