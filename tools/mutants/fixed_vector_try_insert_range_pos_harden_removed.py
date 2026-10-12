"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "fixed_vector_try_insert_range_pos_harden_removed",
    "file": "include/metl/fixed_vector.hpp",
    "why": "the same for try_insert(pos, first, last).",
    "kills": "ctest:harden_floor_memory",
    "old": """        "before anything is written, so that a failure leaves contents unchanged");
    METL_HARDEN(pos >= begin() && pos <= end());""",
    "new": "        \"before anything is written, so that a failure leaves contents unchanged\");",
}
