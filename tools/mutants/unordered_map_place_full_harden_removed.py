"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "unordered_map_place_full_harden_removed",
    "file": "include/metl/static_unordered_map.hpp",
    "why": "a new key into a table with every bucket taken is constructed "
           "at slot npos.",
    "kills": "ctest:harden_floor_memory",
    "old": """    // wild out-of-bounds construct_at(npos, ...).
    METL_HARDEN(index < bucket_count);""",
    "new": "    // wild out-of-bounds construct_at(npos, ...).",
}
