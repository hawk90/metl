"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_ifl_end_position_accepted",
    "file": "include/metl/exp/intrusive_forward_list.hpp",
    "why": "end() passes the position check, so insert_after(end()) and erase_after(end()) write through a null pointer.",
    "kills": "ctest:metl_intrusive_forward_list_test",
    "old": 'return pos.node_ != nullptr && pos.node_->owner == this;',
    "new": 'return pos.node_ == nullptr || pos.node_->owner == this;',
}
