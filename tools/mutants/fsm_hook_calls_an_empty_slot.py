"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::fsm (#367)
MUTANT = {
    "name": "fsm_hook_calls_an_empty_slot",
    "file": "include/metl/fsm.hpp",
    "why": "a hook table slot with no handler is not skipped: an unfilled slot "
           "whose default state matches the state entered is called, and the "
           "hook listed after it is not.",
    "kills": "ctest:fsm_reentrancy",
    "old": """      if (hooks[i].state == state && hooks[i].handler) {""",
    "new": """      if (hooks[i].state == state) {""",
}
