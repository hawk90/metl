"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "arena_rewind_destroys_while_armed",
    "file": "include/metl/arena_allocator.hpp",
    "why": "rewind runs ~T() while the record is still armed, so a destructor "
           "that rewinds the arena destroys the same object again (#312).",
    "kills": "ctest:pool_reentrancy_test",
    "old": """        const destroy_fn_t destroy = record.destroy;
        record.destroy = nullptr;
        store_record(offset_, record);
        destroy(&storage_[record.payload_offset]);
        continue;""",
    "new": """        record.destroy(&storage_[record.payload_offset]);""",
}
