"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_erase_iterator_returns_end",
    "file": "include/metl/flat_map.hpp",
    "why": "erase(iterator) returns end() instead of the next element, so a loop "
           "that erases as it iterates stops after the first erase (#313).",
    "kills": "ctest:associative_erase_iterator_test",
    "old": """    return data() + (index < size_ ? index : size_);""",
    "new": """    return data() + size_;""",
}
