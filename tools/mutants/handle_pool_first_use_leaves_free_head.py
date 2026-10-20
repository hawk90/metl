"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_first_use_leaves_free_head",
    "file": "include/metl/handle_pool.hpp",
    "why": "the free list ends at never_used_; if a first use moves never_used_ "
           "but not free_head_, the next try_emplace pops the slot just handed "
           "out and builds a second object over the first.",
    "kills": "ctest:handle_pool_test",
    "old": """      never_used_ = free_head_ = static_cast<index_type>(index + 1);""",
    "new": """      never_used_ = static_cast<index_type>(index + 1);""",
}
