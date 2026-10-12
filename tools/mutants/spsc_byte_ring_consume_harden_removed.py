"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "spsc_byte_ring_consume_harden_removed",
    "file": "include/metl/spsc_byte_ring.hpp",
    "why": "consume() past the readable bytes moves the read index beyond "
           "the write index at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(count <= tail - head);",
    "new": "",
}
