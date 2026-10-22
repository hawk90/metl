"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_ifl_insert_after_links_at_front",
    "file": "include/metl/exp/intrusive_forward_list.hpp",
    "why": "insert_after links every element at the front, whatever position it is given.",
    "kills": "ctest:metl_intrusive_forward_list_test",
    "old": "    metl::detail::slist_link_after(*pos.node_, node);\n",
    "new": "    metl::detail::slist_link_after(head_, node);\n",
}
