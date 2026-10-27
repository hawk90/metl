"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "ifl_insert_linked_harden_removed",
    "file": "include/metl/intrusive_forward_list.hpp",
    "why": "linking an element that is already linked rewrites its next pointer: its old list loses its tail, or the list becomes a cycle.",
    "kills": "ctest:metl_intrusive_forward_list_test",
    "old": '    METL_HARDEN(node.owner == nullptr);  // already linked\n',
    "new": '',
}
