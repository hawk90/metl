"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_less_ignores_greater_key",
    "file": "include/metl/flat_map.hpp",
    "why": "operator< no longer stops at a greater lhs key, so a later "
           "value or the size decides instead.",
    "kills": "ctest:flat_map_test",
    "old": """    if (rhs.begin()[i].key < lhs.begin()[i].key) {
      return false;
    }""",
    "new": """    if (rhs.begin()[i].key < lhs.begin()[i].key && false) {
      return false;
    }""",
}
