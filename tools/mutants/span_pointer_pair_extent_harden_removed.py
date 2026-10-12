"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "span_pointer_pair_extent_harden_removed",
    "file": "include/metl/span.hpp",
    "why": "a fixed extent over a shorter pointer pair reports more "
           "elements than exist.",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(Extent == dynamic_extent || static_cast<size_type>(last - first) == Extent);",
    "new": "",
}
