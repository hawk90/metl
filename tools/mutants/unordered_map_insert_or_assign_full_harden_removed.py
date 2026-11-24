"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "unordered_map_insert_or_assign_full_harden_removed",
    "file": "include/metl/static_unordered_map.hpp",
    "why": "insert_or_assign of a new key into a full map reads slot_value at the full code's slot, far past the table, at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": "    // A full map has no slot to read; never stripped.\n    METL_HARDEN(!detail::insert_code::is_full(result));",
    "new": "    // A full map has no slot to read; never stripped.",
}
