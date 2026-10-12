"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "fixed_vector_insert_count_pos_harden_removed",
    "file": "include/metl/fixed_vector.hpp",
    "why": "insert(pos, 0, value) past end() returns an iterator outside "
           "the vector.",
    "kills": "ctest:harden_floor_memory",
    "old": """  iterator insert(const_iterator pos, size_type n, const T& value) {
    METL_HARDEN(pos >= begin() && pos <= end());""",
    "new": "  iterator insert(const_iterator pos, size_type n, const T& value) {",
}
