"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_insert_or_assign_full_harden_removed",
    "file": "include/metl/flat_map.hpp",
    "why": "insert_or_assign of a new key into a full flat_map returns a "
           "one-past-the-end reference at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": """      // `index == size_`, which would make this a one-past-the-end reference.
      METL_HARDEN(index < size_);""",
    "new": "      // `index == size_`, which would make this a one-past-the-end reference.",
}
