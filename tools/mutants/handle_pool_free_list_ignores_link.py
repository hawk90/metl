"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_free_list_ignores_link",
    "file": "include/metl/handle_pool.hpp",
    "why": "the free list stores each link relative to i + 1; decoding it as "
           "plain i + 1 still serves a new pool in order, but a freed slot "
           "is never handed out again and the pool runs past its free slots.",
    "kills": "ctest:handle_pool_test",
    "old": """    return static_cast<index_type>(index + 1 + next_[index]);""",
    "new": """    return static_cast<index_type>(index + 1 + 0 * next_[index]);""",
}
