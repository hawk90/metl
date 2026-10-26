"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "il_splice_foreign_element_harden_removed",
    "file": "include/metl/intrusive_list.hpp",
    "why": "splice accepts an element that is not in `other`, so `other`'s size drops for an element it never held.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": "    METL_HARDEN(other.in_this_list(it));    // an element of another list, or none\n    METL_HARDEN(it.node_ != &other.head_);  // end(): there is no element to move\n",
    "new": "    METL_HARDEN(it.node_ != &other.head_);  // end(): there is no element to move\n",
}
