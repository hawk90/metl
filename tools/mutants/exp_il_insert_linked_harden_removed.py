"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_il_insert_linked_harden_removed",
    "file": "include/metl/exp/intrusive_list.hpp",
    "why": "linking an element that is already linked rewires its old neighbours into the new list.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": '    METL_HARDEN(node.owner == nullptr);  // already linked',
    "new": '    // METL_HARDEN(node.owner == nullptr);  // already linked',
}
