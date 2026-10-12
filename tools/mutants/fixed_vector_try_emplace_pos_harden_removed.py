"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "fixed_vector_try_emplace_pos_harden_removed",
    "file": "include/metl/fixed_vector.hpp",
    "why": "try_emplace at a position past end() on a full vector returns "
           "end() instead of reporting the bad position.",
    "kills": "ctest:harden_floor_memory",
    "old": """  METL_NODISCARD iterator try_emplace(const_iterator pos, Args&&... args) {
    METL_HARDEN(pos >= begin() && pos <= end());""",
    "new": "  METL_NODISCARD iterator try_emplace(const_iterator pos, Args&&... args) {",
}
