"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_unwind_keeps_tail",
    "file": "include/metl/handle_pool.hpp",
    "why": "a throwing constructor's unwind returns its slot to an empty free list without "
           "moving the tail there, so after a constructor freed and re-claimed another slot "
           "the tail names that live slot, and the next free links it into the list (#642).",
    "kills": "ctest:pool_reentrancy_test",
    "old": """        if (pool_->free_head_ >= Capacity) {
          pool_->free_tail_ = index_;
        }
""",
    "new": "",
}
