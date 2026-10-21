"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_ifl_erase_foreign_element_harden_removed",
    "file": "include/metl/exp/intrusive_forward_list.hpp",
    "why": "erase of an element in another list, or in none, walks this list past its end looking for it.",
    "kills": "ctest:metl_intrusive_forward_list_test",
    "old": '    METL_HARDEN(node.owner == this);  // in another list, or in none\n',
    "new": '',
}
