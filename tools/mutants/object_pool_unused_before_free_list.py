"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "object_pool_unused_before_free_list",
    "file": "include/metl/object_pool.hpp",
    "why": "try_emplace() takes a never-used slot before a freed one, so the "
           "documented reuse order (the slot freed last comes back first) breaks.",
    "kills": "ctest:object_pool_test",
    "old": """    if (free_head_ != 0) {
      i = free_head_ - 1;
      free_head_ = link(i);
    } else if (unused_ < Capacity) {
      i = unused_++;
    } else {""",
    "new": """    if (unused_ < Capacity) {
      i = unused_++;
    } else if (free_head_ != 0) {
      i = free_head_ - 1;
      free_head_ = link(i);
    } else {""",
}
