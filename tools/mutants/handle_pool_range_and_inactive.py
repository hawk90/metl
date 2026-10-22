"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_range_and_inactive",
    "file": "include/metl/handle_pool.hpp",
    "why": "the liveness check drops the null test, so the null handle, "
           "whose generation is the free marker 0, resolves to a free slot 0.",
    "kills": "ctest:handle_pool_test",
    "old": """    if (!handle.valid()) {
      return Capacity;
    }""",
    "new": """    if (false) {
      return Capacity;
    }""",
}
