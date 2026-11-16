"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_clear_relocates",
    "file": "include/metl/flat_map.hpp",
    "why": "clear() moves each element into a temporary before destroying it: "
           "N needless moves, and std::terminate when the move throws.",
    "kills": "ctest:throwing_element",
    "old": """    while (comp_size_.value > 0) {
      --comp_size_.value;
      data()[comp_size_.value].~value_type();
    }""",
    "new": """    while (comp_size_.value > 0) {
      erase_at(comp_size_.value - 1);
    }""",
}
