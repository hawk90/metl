"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "unordered_map_heterogeneous_erase_missing_true",
    "file": "include/metl/static_unordered_map.hpp",
    "why": "heterogeneous erase of an absent key reports that it erased "
           "something.",
    "kills": "ctest:static_unordered_map_test",
    "old": """  bool erase(const K& key) noexcept(lookup_cannot_throw<K>) {
    const size_type index = find_existing_index(key);
    if (index == npos) {
      return false;""",
    "new": """  bool erase(const K& key) noexcept(lookup_cannot_throw<K>) {
    const size_type index = find_existing_index(key);
    if (index == npos) {
      return true;""",
}
