"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "monotonic_zero_capacity_alignment_harden_removed",
    "file": "include/metl/monotonic_buffer.hpp",
    "why": "the zero-byte buffer (#696) stops checking the alignment, so a "
           "non-power-of-two one passes silently there while every other "
           "buffer traps it.",
    "kills": "ctest:zero_capacity_test",
    "old": """    METL_HARDEN(power_of_two);
    return nullptr;""",
    "new": """    (void)power_of_two;
    return nullptr;""",
}
