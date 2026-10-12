"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "arena_alignment_harden_removed",
    "file": "include/metl/arena_allocator.hpp",
    "why": "a non-power-of-two alignment reaches align_up's bitmask at "
           "METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(alignment != 0 && (alignment & (alignment - 1)) == 0);",
    "new": "",
}
