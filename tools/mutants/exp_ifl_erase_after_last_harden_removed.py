"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_ifl_erase_after_last_harden_removed",
    "file": "include/metl/exp/intrusive_forward_list.hpp",
    "why": "erase_after the last element, or pop_front on an empty list, unlinks through a null pointer.",
    "kills": "ctest:metl_intrusive_forward_list_test",
    "old": '    METL_HARDEN(pos.node_->next != nullptr);  // nothing after pos\n',
    "new": '',
}
