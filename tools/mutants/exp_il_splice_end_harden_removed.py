"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_il_splice_end_harden_removed",
    "file": "include/metl/exp/intrusive_list.hpp",
    "why": "splice accepts `other.end()`, so it unlinks the list's head and links it in as an element.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": "    METL_HARDEN(it.node_ != &other.head_);  // end(): there is no element to move\n",
    "new": "",
}
