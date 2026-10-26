"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "ifl_insert_foreign_position_harden_removed",
    "file": "include/metl/intrusive_forward_list.hpp",
    "why": "insert_after with a position in another list links the element into that list while recording this one as its owner, and counts it here.",
    "kills": "ctest:metl_intrusive_forward_list_test",
    "old": '    METL_HARDEN(in_this_list(pos));      // a position in another list, or end()\n    metl::detail::slist_link_after',
    "new": '    metl::detail::slist_link_after',
}
