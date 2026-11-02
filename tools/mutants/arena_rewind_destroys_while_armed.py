"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "arena_rewind_destroys_while_armed",
    "file": "include/metl/arena_allocator.hpp",
    "why": "rewind runs ~T() while the record is still armed, so a destructor "
           "that rewinds the arena destroys the same object again (#312).",
    "kills": "ctest:pool_reentrancy_test",
    "old": """        record.destroy = nullptr;
        record.payload_offset = destroying;
        store_record(at, record);""",
    "new": """        record.payload_offset = destroying;""",
}
