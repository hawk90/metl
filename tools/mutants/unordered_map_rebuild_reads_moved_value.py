"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::static_unordered_map (#390)
MUTANT = {
    "name": "unordered_map_rebuild_reads_moved_value",
    "file": "include/metl/static_unordered_map.hpp",
    "why": "an insert that triggers the tombstone rebuild builds the new element "
           "after the rebuild, so a value argument that refers to a held element "
           "is read after the rebuild moved it.",
    "kills": "ctest:conformance_alias",
    "old": """      value_type entry{std::forward<K>(key), std::forward<V>(value)};
      rehash_in_place();""",
    "new": """      rehash_in_place();
      value_type entry{std::forward<K>(key), std::forward<V>(value)};""",
}
