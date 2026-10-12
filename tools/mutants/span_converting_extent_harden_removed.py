"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "span_converting_extent_harden_removed",
    "file": "include/metl/span.hpp",
    "why": "a fixed extent over a shorter span reports more elements than "
           "exist.",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(Extent == dynamic_extent || other.size() == Extent);",
    "new": "",
}
