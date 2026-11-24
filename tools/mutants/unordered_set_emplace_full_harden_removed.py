"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "unordered_set_emplace_full_harden_removed",
    "file": "include/metl/static_unordered_set.hpp",
    "why": "emplace of a new key into a full static_unordered_set hands back {end(), false} instead of trapping, at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": "    // Hard guard, never stripped: a full set has no element to point at.\n    METL_HARDEN(result.first != end());",
    "new": "    // Hard guard, never stripped: a full set has no element to point at.",
}
