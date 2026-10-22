"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_il_splice_keeps_old_owner",
    "file": "include/metl/exp/intrusive_list.hpp",
    "why": "an element spliced into another list still names its old list, so erasing it through the new one traps and through the old one corrupts both.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": "      it.node_->owner = this;\n",
    "new": "",
}
