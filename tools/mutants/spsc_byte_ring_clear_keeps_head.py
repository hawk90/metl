"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "spsc_byte_ring_clear_keeps_head",
    "file": "include/metl/spsc_byte_ring.hpp",
    "why": "clear() resets only the write index; after any consume the "
           "read index is left ahead of it and readable_size() wraps.",
    "kills": "ctest:spsc_byte_ring_test",
    "old": """  void clear() noexcept {
    head_.store(0, std::memory_order_relaxed);
""",
    "new": """  void clear() noexcept {
""",
}
