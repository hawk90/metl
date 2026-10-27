"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "il_splice_foreign_position_harden_removed",
    "file": "include/metl/intrusive_list.hpp",
    "why": "splice accepts a position in another list, so the element is linked into that list while this one owns it and counts it.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": "    METL_HARDEN(in_this_list(pos));         // a position in another list, or none\n    METL_HARDEN(other.in_this_list(it));    // an element of another list, or none\n",
    "new": "    METL_HARDEN(other.in_this_list(it));    // an element of another list, or none\n",
}
