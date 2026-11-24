"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_insert_or_assign_full_harden_removed",
    "file": "include/metl/flat_map.hpp",
    "why": "insert_or_assign of a new key into a full flat_map returns a reference through end(), one past the last element, at METL_HARDENING_NONE (#580).",
    "kills": "ctest:harden_floor_memory",
    "old": "    // Same full-map hazard as emplace above (#580).\n    METL_HARDEN(result.first != end());",
    "new": "    // Same full-map hazard as emplace above (#580).",
}
