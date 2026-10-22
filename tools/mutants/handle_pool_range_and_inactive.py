"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_range_and_inactive",
    "file": "include/metl/handle_pool.hpp",
    "why": "the liveness check drops the parity test, so a handle forged "
           "with a free slot's even generation resolves to that slot.",
    "kills": "ctest:handle_pool_test",
    "old": "    if ((handle.generation() & generation_type{1}) == 0) {",
    "new": "    if (handle.generation() == 0) {",
}
