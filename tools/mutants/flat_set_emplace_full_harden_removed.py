"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_set_emplace_full_harden_removed",
    "file": "include/metl/flat_set.hpp",
    "why": "emplace of a new key into a full flat_set hands back {end(), false} instead of trapping, at METL_HARDENING_NONE (#580).",
    "kills": "ctest:harden_floor_memory",
    "old": "    // Hard guard, never stripped; see the twin on flat_map::emplace.\n    METL_HARDEN(!detail::insert_code::is_full(result));",
    "new": "    // Hard guard, never stripped; see the twin on flat_map::emplace.",
}
