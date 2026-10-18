"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_first_use_keeps_generation_zero",
    "file": "include/metl/handle_pool.hpp",
    "why": "a new pool's generations are stored as 0, read as 1; without the "
           "store on first use, the first handle from a slot carries "
           "generation 0, the null handle's, and never resolves.",
    "kills": "ctest:handle_pool_test",
    "old": """    if (generation_[index] == generation_type{0}) {
      generation_[index] = generation_type{1};  // first use of the slot
    }""",
    "new": """    if (generation_[index] == generation_type{0}) {
    }""",
}
