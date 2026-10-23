"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_link_before_destructor",
    "file": "include/metl/handle_pool.hpp",
    "why": "release() writes the free-list link into the slot before ~T() "
           "runs, over the bytes of an object that is still alive.",
    "kills": "ctest:handle_pool_test",
    "old": """    slot_ptr(index)->~T();

    set_link(index, free_head_);""",
    "new": """    set_link(index, free_head_);

    slot_ptr(index)->~T();
""",
}
