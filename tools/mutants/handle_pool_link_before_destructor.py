"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_link_before_destructor",
    "file": "include/metl/handle_pool.hpp",
    "why": "release() writes the free-list link into the slot before ~T() "
           "runs, over the bytes of an object that is still alive.",
    "kills": "ctest:handle_pool_test",
    "old": """    slot_ptr(index)->~T();

    set_free(index, free_head_, next);""",
    "new": """    set_free(index, free_head_, next);

    slot_ptr(index)->~T();
""",
}
