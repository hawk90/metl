"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "arena_offset_skips_a_byte",
    "file": "include/metl/arena_allocator.hpp",
    "why": "every allocation consumes one byte more than it reports; "
           "rewind and reset stay self-consistent, only used() and the "
           "offsets move.",
    "kills": "ctest:arena_allocator_test",
    "old": "    const size_type next_offset = previous_offset + total_bytes;",
    "new": "    const size_type next_offset = previous_offset + total_bytes + 1;",
}
