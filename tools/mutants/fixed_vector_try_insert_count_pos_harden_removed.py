"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "fixed_vector_try_insert_count_pos_harden_removed",
    "file": "include/metl/fixed_vector.hpp",
    "why": "the same for try_insert(pos, n, value).",
    "kills": "ctest:harden_floor_memory",
    "old": """  METL_NODISCARD iterator try_insert(const_iterator pos, size_type n, const T& value) {
    METL_HARDEN(pos >= begin() && pos <= end());""",
    "new": "  METL_NODISCARD iterator try_insert(const_iterator pos, size_type n, const T& value) {",
}
