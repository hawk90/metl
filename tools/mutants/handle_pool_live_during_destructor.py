"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_live_during_destructor",
    "file": "include/metl/handle_pool.hpp",
    "why": "release() marks the slot free only after ~T() returns, so while "
           "the destructor runs the slot still reads as live and a clear() "
           "from that destructor destroys the object a second time.",
    "kills": "ctest:metl_conformance_removal_test",
    "old": """    generation_[index] = generation_type{0};
    --size_;

    slot_ptr(index)->~T();
""",
    "new": """    --size_;

    slot_ptr(index)->~T();
    generation_[index] = generation_type{0};
""",
}
