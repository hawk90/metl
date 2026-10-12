"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "fixed_priority_queue_pop_empty_harden_removed",
    "file": "include/metl/fixed_priority_queue.hpp",
    "why": "pop on an empty queue moves from index SIZE_MAX into slot 0 "
           "before the vector's own check fires.",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(!storage_.empty());",
    "new": "",
}
