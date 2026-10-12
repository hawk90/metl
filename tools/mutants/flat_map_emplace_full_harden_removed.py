"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_emplace_full_harden_removed",
    "file": "include/metl/flat_map.hpp",
    "why": "emplace of a new key into a full flat_map returns a one-past- "
           "the-end reference at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": """      // stripped at low hardening levels; METL_HARDEN never is.
      METL_HARDEN(index < size_);""",
    "new": "      // stripped at low hardening levels; METL_HARDEN never is.",
}
