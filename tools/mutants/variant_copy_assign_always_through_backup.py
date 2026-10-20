"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "variant_copy_assign_always_through_backup",
    "file": "include/metl/variant.hpp",
    "why": "Copy-assignment to another alternative builds a backup variant inline, "
           "a whole alternative of stack in the frame of every switch (#315).",
    "kills": "ctest:variant_stack_frame",
    "old": """    if (copies_in_place()[other.index_]) {""",
    "new": """    if (other.index_ != variant_npos) {
      variant backup(other);
      reset();
      move_ops()[backup.index_](raw_addr(), backup.raw_addr());
      index_ = backup.index_;
      return;
    }
    if (copies_in_place()[other.index_]) {""",
}
