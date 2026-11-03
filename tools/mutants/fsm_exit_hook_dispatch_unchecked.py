"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::fsm (#636)
MUTANT = {
    "name": "fsm_exit_hook_dispatch_unchecked",
    "file": "include/metl/fsm.hpp",
    "why": "a dispatch from an exit hook is not refused: the state being left is "
           "still current, so the same transition fires again and the recursion "
           "never ends.",
    "kills": "ctest:fsm_reentrancy",
    "old": """    METL_ASSERT(!exiting_);
""",
    "new": "",
}
