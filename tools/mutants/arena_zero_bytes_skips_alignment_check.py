"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "arena_zero_bytes_skips_alignment_check",
    "file": "include/metl/arena_allocator.hpp",
    "why": "an empty request returns null before the alignment is checked, "
           "hiding a caller bug monotonic_buffer reports.",
    "kills": "ctest:harden_floor_memory",
    "old": """    METL_HARDEN(alignment != 0 && (alignment & (alignment - 1)) == 0);
    if (bytes == 0) {
      return nullptr;
    }
""",
    "new": """    if (bytes == 0) {
      return nullptr;
    }
    METL_HARDEN(alignment != 0 && (alignment & (alignment - 1)) == 0);
""",
}
