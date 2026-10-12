"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "span_subspan_offset_harden_removed",
    "file": "include/metl/span.hpp",
    "why": "subspan<Offset>() past the end views memory beyond the "
           "storage.",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(Offset <= size());",
    "new": "",
}
