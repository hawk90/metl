"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_priority_queue_comparator_uninitialized",
    "file": "include/metl/fixed_priority_queue.hpp",
    "why": "a default-initialized queue leaves a stateful comparator indeterminate, "
           "so the heap order depends on whatever bytes were there (#574).",
    "kills": "ctest:fixed_priority_queue_test",
    "old": "  Compare comp_{};\n",
    "new": "  Compare comp_;\n",
}
