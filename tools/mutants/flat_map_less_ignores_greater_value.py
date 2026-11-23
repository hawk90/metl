"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_less_ignores_greater_value",
    "file": "include/metl/flat_map.hpp",
    "why": "operator< no longer stops at a greater lhs value, so the size "
           "decides instead.",
    "kills": "ctest:flat_map_test",
    "old": """    if (rhs.begin().operator->()[i].value < lhs.begin().operator->()[i].value) {
      return false;
    }""",
    "new": """    if (rhs.begin().operator->()[i].value < lhs.begin().operator->()[i].value && false) {
      return false;
    }""",
}
