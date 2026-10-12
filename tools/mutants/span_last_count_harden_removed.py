"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "span_last_count_harden_removed",
    "file": "include/metl/span.hpp",
    "why": "last<Count>() longer than the span starts before the storage.",
    "kills": "ctest:harden_floor_memory",
    "old": """"last<Count>(): Count must not exceed Extent");
    METL_HARDEN(Count <= size());""",
    "new": "\"last<Count>(): Count must not exceed Extent\");",
}
