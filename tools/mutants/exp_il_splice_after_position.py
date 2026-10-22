"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_il_splice_after_position",
    "file": "include/metl/exp/intrusive_list.hpp",
    "why": "splice moves the element after `pos` instead of before it.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": "    metl::detail::dlist_link_before(*pos.node_, *it.node_);\n",
    "new": "    metl::detail::dlist_link_before(*pos.node_->next, *it.node_);\n",
}
