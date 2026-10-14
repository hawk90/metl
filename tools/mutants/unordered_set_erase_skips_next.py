"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::static_unordered_set (#391)
MUTANT = {
    "name": "unordered_set_erase_skips_next",
    "file": "include/metl/static_unordered_set.hpp",
    "why": "erase(iterator) resumes the scan two slots on, so when the next "
           "element sits in the adjacent slot it is skipped instead of returned.",
    "kills": "ctest:conformance_std_parity",
    "old": """    destroy_at(index, slot_state::tombstone);
    return iterator(this, index + 1);""",
    "new": """    destroy_at(index, slot_state::tombstone);
    return iterator(this, index + 2);""",
}
