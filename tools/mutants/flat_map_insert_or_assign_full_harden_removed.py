"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_insert_or_assign_full_harden_removed",
    "file": "include/metl/flat_map.hpp",
    "why": "insert_or_assign of a new key into a full flat_map reads data()[slot] of the full code, far past the end, at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": "    // Same full-map hazard as emplace above (#580).\n    METL_HARDEN(!detail::insert_code::is_full(result));",
    "new": "    // Same full-map hazard as emplace above (#580).",
}
