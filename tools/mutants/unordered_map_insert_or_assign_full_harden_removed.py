"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "unordered_map_insert_or_assign_full_harden_removed",
    "file": "include/metl/static_unordered_map.hpp",
    "why": "insert_or_assign of a new key into a full map returns "
           "*slot_value(npos) at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": """      // hardening levels; without this, slot_value(npos) would be a wild read.
      METL_HARDEN(index < bucket_count);""",
    "new": """      // hardening levels; without this, slot_value(npos) would be a wild read.""",
}
