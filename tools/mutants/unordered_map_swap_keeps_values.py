"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::static_unordered_map swap (#348)
MUTANT = {
    "name": "unordered_map_swap_keeps_values",
    "file": "include/metl/static_unordered_map.hpp",
    "why": "In a slot both tables hold, swap exchanges the keys but not the mapped "
           "values, so each key ends up with the other table's value.",
    "kills": "ctest:container_swap",
    "old": """        swap(slot_value(i)->key, other.slot_value(i)->key);
        swap(slot_value(i)->value, other.slot_value(i)->value);""",
    "new": """        swap(slot_value(i)->key, other.slot_value(i)->key);""",
}
