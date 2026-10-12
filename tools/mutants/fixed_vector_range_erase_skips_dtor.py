"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_vector_range_erase_skips_dtor",
    "file": "include/metl/fixed_vector.hpp",
    "why": "range erase shifts the survivors down but never destroys the "
           "vacated tail, so those objects leak.",
    "kills": "ctest:fixed_vector_test|sequence_reentrancy",
    "old": """    for (size_type i = size_; i < old_size; ++i) {
      destroy_element(i);
    }""",
    "new": """    for (size_type i = size_; i < old_size; ++i) {
    }""",
}
