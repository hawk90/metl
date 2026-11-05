"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "spsc_byte_ring_storage_alignment_ignored",
    "file": "include/metl/spsc_byte_ring.hpp",
    "why": "The storage keeps METL_CACHELINE_SIZE's alignment whatever "
           "StorageAlignment asks for, so on a Cortex-M7 a cache line the "
           "storage covers also holds head_ and tail_, and invalidating it for "
           "DMA loses an index.",
    "kills": "ctest:spsc_byte_ring_test",
    "old": """  alignas(StorageAlignment > METL_CACHELINE_SIZE ? StorageAlignment
                                                 : METL_CACHELINE_SIZE) std::byte storage_[Capacity] = {};
""",
    "new": """  alignas(METL_CACHELINE_SIZE) std::byte storage_[Capacity] = {};
""",
}
