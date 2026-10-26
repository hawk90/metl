"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "ifl_erase_after_foreign_position_harden_removed",
    "file": "include/metl/intrusive_forward_list.hpp",
    "why": "erase_after with a position in another list unlinks from that list and decrements this list's size.",
    "kills": "ctest:metl_intrusive_forward_list_test",
    "old": '    METL_HARDEN(in_this_list(pos));           // a position in another list, or end()\n    METL_HARDEN(pos.node_->next != nullptr);',
    "new": '    METL_HARDEN(pos.node_->next != nullptr);',
}
