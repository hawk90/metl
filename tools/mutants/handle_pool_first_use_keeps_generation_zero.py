"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_first_use_keeps_generation_zero",
    "file": "include/metl/handle_pool.hpp",
    "why": "a new pool stores every generation as 0; without the store on a "
           "slot's first use, its first handle carries generation 0, the null "
           "handle's, and never resolves.",
    "kills": "ctest:handle_pool_test",
    "old": """      generation_[index] = generation_type{1};
    } else {""",
    "new": """    } else {""",
}
