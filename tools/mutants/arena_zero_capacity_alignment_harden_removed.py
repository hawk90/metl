"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "arena_zero_capacity_alignment_harden_removed",
    "file": "include/metl/arena_allocator.hpp",
    "why": "the zero-byte arena (#696) stops checking the alignment, so a "
           "non-power-of-two one passes silently there while every other "
           "arena traps it.",
    "kills": "ctest:zero_capacity_test",
    "old": """    METL_HARDEN(power_of_two);
    return nullptr;""",
    "new": """    (void)power_of_two;
    return nullptr;""",
}
