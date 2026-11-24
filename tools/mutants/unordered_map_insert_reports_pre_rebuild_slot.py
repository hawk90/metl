"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "unordered_map_insert_reports_pre_rebuild_slot",
    "file": "include/metl/static_unordered_map.hpp",
    "why": "try_emplace and emplace hand back the slot the key was headed for before a tombstone rebuild moved every element, so the iterator points at another key.",
    "kills": "ctest:associative_insert_result",
    "old": "      index = construct_at(index, std::forward<K>(key), std::forward<V>(value));\n      return {iterator(this, index, detail::at_slot), true};\n    }\n  }\n\n  /// @brief Insert @p key/@p value if @p key is absent.",
    "new": "      (void)construct_at(index, std::forward<K>(key), std::forward<V>(value));\n      return {iterator(this, index, detail::at_slot), true};\n    }\n  }\n\n  /// @brief Insert @p key/@p value if @p key is absent.",
}
