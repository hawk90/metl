"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::arena_allocator (#637)
MUTANT = {
    "name": "arena_stale_mark_unchecked",
    "file": "include/metl/arena_allocator.hpp",
    "why": "rewind to an invalidated mark is not refused: the walk releases the "
           "record that straddles the mark and every byte below it, destroying "
           "objects allocated under the mark.",
    "kills": "ctest:utility_assert_trips",
    "old": """      METL_HARDEN(record.previous_offset >= target.offset);
""",
    "new": "",
}
