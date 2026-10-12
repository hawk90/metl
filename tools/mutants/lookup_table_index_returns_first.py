"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "lookup_table_index_returns_first",
    "file": "include/metl/lookup_table.hpp",
    "why": "operator[] returns entry 0 whatever the index. The constexpr "
           "checks never call it, and the one other call reads a "
           "character entry 0 happens to share.",
    "kills": "ctest:lookup_table_test",
    "old": """operator[](size_type index) const noexcept {
    METL_ASSERT(index < Size);
    return entries_[index];""",
    "new": """operator[](size_type index) const noexcept {
    METL_ASSERT(index < Size);
    return entries_[0];""",
}
