"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_insert_or_assign_full_harden_removed",
    "file": "include/metl/flat_map.hpp",
    "why": "insert_or_assign of a new key into a full flat_map returns a "
           "one-past-the-end reference at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": '      // Same full-map hazard as emplace above (#580).\n      METL_HARDEN(stored);',
    "new": '      // Same full-map hazard as emplace above (#580).\n      (void)stored;',
}
