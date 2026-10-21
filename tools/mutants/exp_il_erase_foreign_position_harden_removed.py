"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_il_erase_foreign_position_harden_removed",
    "file": "include/metl/exp/intrusive_list.hpp",
    "why": "erase at a position in another list unlinks that list's element while this one counts it gone.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": '    METL_HARDEN(in_this_list(pos));    // a position in another list, or none',
    "new": '    // METL_HARDEN(in_this_list(pos));    // a position in another list, or none',
}
