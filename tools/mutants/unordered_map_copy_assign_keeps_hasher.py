"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "unordered_map_copy_assign_keeps_hasher",
    "file": "include/metl/static_unordered_map.hpp",
    "why": "copy assignment keeps the destination's hasher instead of the "
           "source's, unlike the copy constructor.",
    "kills": "ctest:static_unordered_map_test",
    "old": """    clear();
    hasher_ = other.hasher_;
""",
    "new": """    clear();
""",
}
