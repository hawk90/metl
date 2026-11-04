"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "monotonic_allocate_n_count_wraps",
    "file": "include/metl/monotonic_buffer.hpp",
    "why": "try_allocate_n forms sizeof(T) * count without the overflow check, "
           "so a huge count wraps into a small request that fits.",
    "kills": "ctest:allocator_overflow_test",
    "old": "if (count == 0 || count > static_cast<size_type>(-1) / sizeof(T)) {",
    "new": "if (count == 0) {",
}
