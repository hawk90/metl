"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "il_destroy_linked_harden_removed",
    "file": "include/metl/intrusive_list.hpp",
    "why": "an element destroyed while linked leaves its list pointing at a dead object, and nothing stops it.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": '  ~list_hook() { METL_HARDEN(owner == nullptr); }  // destroyed while linked',
    "new": '  ~list_hook() {}  // destroyed while linked',
}
