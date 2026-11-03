"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::handle_pool (#642)
MUTANT = {
    "name": "handle_pool_free_list_lifo",
    "file": "include/metl/handle_pool.hpp",
    "why": "a released slot goes to the front of the free list, so the slot "
           "freed last is reused first and churn wraps that one slot's "
           "generation 'Capacity' times sooner.",
    "kills": "ctest:handle_pool_test",
    "old": """    slot_ptr(index)->~T();

    append_free(index);""",
    "new": """    slot_ptr(index)->~T();

    set_link(index, free_head_);
    free_head_ = index;""",
}
