"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_emplace_full_harden_removed",
    "file": "include/metl/flat_map.hpp",
    "why": "emplace of a new key into a full flat_map hands back {end(), false} instead of trapping, at METL_HARDENING_NONE (#580).",
    "kills": "ctest:harden_floor_memory",
    "old": "    // Hard guard, never stripped: a full map has no element to point at (#580).\n    METL_HARDEN(!detail::insert_code::is_full(result));",
    "new": "    // Hard guard, never stripped: a full map has no element to point at (#580).",
}
