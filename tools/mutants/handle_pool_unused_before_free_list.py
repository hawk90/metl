"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_unused_before_free_list",
    "file": "include/metl/handle_pool.hpp",
    "why": "try_emplace() takes a never-used slot before a freed one, so the "
           "documented reuse order (the slot freed last comes back first) breaks.",
    "kills": "ctest:handle_pool_test",
    "old": """    if (free_head_ != 0) {
      index = free_head_ - 1U;
      free_head_ = static_cast<index_type>(link(index));
    } else if (unused_ < Capacity) {
      index = unused_++;
    } else {""",
    "new": """    if (unused_ < Capacity) {
      index = unused_++;
    } else if (free_head_ != 0) {
      index = free_head_ - 1U;
      free_head_ = static_cast<index_type>(link(index));
    } else {""",
}
