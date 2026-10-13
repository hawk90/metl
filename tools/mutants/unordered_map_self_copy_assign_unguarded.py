"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::static_unordered_map (#366)
MUTANT = {
    "name": "unordered_map_self_copy_assign_unguarded",
    "file": "include/metl/static_unordered_map.hpp",
    "why": "copy-assignment loses its self-assignment check: `m = m` clears the "
           "map and then copies from the now empty map.",
    "kills": "ctest:conformance_self",
    "old": """  static_unordered_map& operator=(const static_unordered_map& other) {
    if (this == &other) {
      return *this;
    }

    clear();""",
    "new": """  static_unordered_map& operator=(const static_unordered_map& other) {
    clear();""",
}
