"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::flat_map swap (#348)
MUTANT = {
    "name": "flat_map_swap_throw_keeps_other",
    "file": "include/metl/flat_map.hpp",
    "why": "When an element move throws part-way through swap, only this map is "
           "cleared, and the other keeps an array that is no longer sorted.",
    "kills": "ctest:throwing_element",
    "old": """        clear();
        other.clear();
        throw;""",
    "new": """        clear();
        throw;""",
}
