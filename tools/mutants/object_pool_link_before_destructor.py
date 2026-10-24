"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "object_pool_link_before_destructor",
    "file": "include/metl/object_pool.hpp",
    "why": "end_life() writes the free-list link into the slot before ~T() "
           "runs, over the bytes of an object that is still alive.",
    "kills": "ctest:object_pool_test",
    "old": """    slot_ptr(index)->~T();
    push_free(index);""",
    "new": """    push_free(index);
    slot_ptr(index)->~T();""",
}
