"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "arena_exact_fit_record_rejected",
    "file": "include/metl/arena_allocator.hpp",
    "why": "an allocation whose payload and record fill the arena to the "
           "last byte is refused.",
    "kills": "ctest:arena_allocator_test",
    "old": "    if (sizeof(allocation_record) > left) {",
    "new": "    if (sizeof(allocation_record) >= left) {",
}
