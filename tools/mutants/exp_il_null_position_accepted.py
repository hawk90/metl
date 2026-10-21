"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_il_null_position_accepted",
    "file": "include/metl/exp/intrusive_list.hpp",
    "why": "a default-constructed iterator passes the position check, so insert writes through a null pointer.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": 'return pos.node_ != nullptr && pos.node_->owner == this;',
    "new": 'return pos.node_ == nullptr || pos.node_->owner == this;',
}
