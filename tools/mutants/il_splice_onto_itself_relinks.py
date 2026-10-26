"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "il_splice_onto_itself_relinks",
    "file": "include/metl/intrusive_list.hpp",
    "why": "splicing an element before itself unlinks it and then links it before its own cleared link.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": "    if (pos.node_ == it.node_ || pos.node_ == it.node_->next) {\n",
    "new": "    if (pos.node_ == it.node_->next) {\n",
}
