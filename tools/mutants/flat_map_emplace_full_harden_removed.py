"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_emplace_full_harden_removed",
    "file": "include/metl/flat_map.hpp",
    "why": "emplace of a new key into a full flat_map returns a one-past- "
           "the-end reference, or another key's element (#580), at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": "      // would be one past the end, or another key's element (#580).\n      METL_HARDEN(inserted);",
    "new": "      // would be one past the end, or another key's element (#580).\n      (void)inserted;",
}
