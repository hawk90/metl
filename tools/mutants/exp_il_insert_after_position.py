"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_il_insert_after_position",
    "file": "include/metl/exp/intrusive_list.hpp",
    "why": "insert links the element after `pos` instead of before it, so the list's order differs from std::list's.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": "    metl::detail::dlist_link_before(*pos.node_, node);\n",
    "new": "    metl::detail::dlist_link_before(*pos.node_->next, node);\n",
}
