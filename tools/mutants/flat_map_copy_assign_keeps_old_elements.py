"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::flat_map (#366)
MUTANT = {
    "name": "flat_map_copy_assign_keeps_old_elements",
    "file": "include/metl/flat_map.hpp",
    "why": "copy-assignment does not clear the target first: its old elements "
           "stay alongside the copied ones.",
    "kills": "ctest:conformance_copy",
    "old": """    clear();
    comp_ = other.comp_;
    // `other` is sorted and unique: append, comparing nothing (#644).
    append_copies_from(other);""",
    "new": """    comp_ = other.comp_;
    // `other` is sorted and unique: append, comparing nothing (#644).
    append_copies_from(other);""",
}
