"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "il_erase_foreign_element_harden_removed",
    "file": "include/metl/intrusive_list.hpp",
    "why": "erase(t) of an element in another list unlinks it there while this list counts it gone.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": '    METL_HARDEN(node.owner == this);  // in another list, or in none',
    "new": '    // METL_HARDEN(node.owner == this);  // in another list, or in none',
}
