"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "il_insert_foreign_position_harden_removed",
    "file": "include/metl/intrusive_list.hpp",
    "why": "insert at a position in another list links the element into that list while this one counts it.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": '    METL_HARDEN(in_this_list(pos));      // a position in another list, or none',
    "new": '    // METL_HARDEN(in_this_list(pos));      // a position in another list, or none',
}
