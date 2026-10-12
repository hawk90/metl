"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "span_pointer_pair_reversed_harden_removed",
    "file": "include/metl/span.hpp",
    "why": "a reversed pointer pair makes a span whose size() wraps to "
           "nearly SIZE_MAX.",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(last >= first);",
    "new": "",
}
