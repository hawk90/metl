"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "span_subspan_count_harden_removed",
    "file": "include/metl/span.hpp",
    "why": "subspan<Offset, Count>() longer than what remains views memory "
           "beyond the storage.",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(actual_count <= (size() - Offset));",
    "new": "",
}
