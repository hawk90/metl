"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_range_and_inactive",
    "file": "include/metl/handle_pool.hpp",
    "why": "the liveness check needs out-of-range AND inactive to reject, "
           "so a forged handle to a never-used slot at its starting "
           "generation resolves.",
    "kills": "ctest:handle_pool_test",
    "old": "if (index >= Capacity || next_[index] != index || generation_[index] != handle.generation()) {",
    "new": "if ((index >= Capacity && next_[index] != index) || generation_[index] != handle.generation()) {",
}
