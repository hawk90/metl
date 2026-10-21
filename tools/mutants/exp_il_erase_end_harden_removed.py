"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_il_erase_end_harden_removed",
    "file": "include/metl/exp/intrusive_list.hpp",
    "why": "erase(end()) unlinks the head itself, which leaves the list pointing nowhere.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": '    METL_HARDEN(pos.node_ != &head_);  // end(): there is no element to unlink',
    "new": '    // METL_HARDEN(pos.node_ != &head_);  // end(): there is no element to unlink',
}
