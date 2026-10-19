"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::static_unordered_set swap (#348)
MUTANT = {
    "name": "unordered_set_swap_leaks_moved",
    "file": "include/metl/static_unordered_set.hpp",
    "why": "An element in a slot only one table holds is moved across but the "
           "moved-from object is never destroyed.",
    "kills": "ctest:throwing_element",
    "old": """        to.states_[i] = slot_state::occupied;
        from.slot_value(i)->~value_type();""",
    "new": """        to.states_[i] = slot_state::occupied;""",
}
