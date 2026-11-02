"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "arena_nested_rewind_passes_running_object",
    "file": "include/metl/arena_allocator.hpp",
    "why": "a rewind nested in a destructor releases the storage under the object "
           "being destroyed, and the destructor's next allocation lands on it (#572).",
    "kills": "ctest:pool_reentrancy_test",
    "old": """        // its storage once that returns.
        return;""",
    "new": """        // its storage once that returns.
        offset_ = record.previous_offset;
        continue;""",
}
