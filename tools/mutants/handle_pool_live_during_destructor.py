"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_live_during_destructor",
    "file": "include/metl/handle_pool.hpp",
    "why": "release() leaves the slot's self-link in place while ~T() runs, "
           "so the slot still reads as live and a clear() from that "
           "destructor destroys the object a second time.",
    "kills": "ctest:metl_conformance_removal_test",
    "old": """    next_[index] = static_cast<index_type>(Capacity);  // not live, and not yet free
""",
    "new": "",
}
