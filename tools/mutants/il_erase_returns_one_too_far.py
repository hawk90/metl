"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "il_erase_returns_one_too_far",
    "file": "include/metl/intrusive_list.hpp",
    "why": "erase(pos) returns the element two after the erased one, where std::list returns the next.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": "    link* const next = pos.node_->next;\n",
    "new": "    link* const next = pos.node_->next == &head_ ? &head_ : pos.node_->next->next;\n",
}
