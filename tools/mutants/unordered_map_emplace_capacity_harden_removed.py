"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "unordered_map_emplace_capacity_harden_removed",
    "file": "include/metl/static_unordered_map.hpp",
    "why": "emplace and try_emplace insert a new key past Capacity while a free slot is left, so size() exceeds it and full() answers false (#579).",
    "kills": "ctest:harden_floor_memory",
    "old": "      // Refuse a new key past Capacity even when a free slot is left.\n      if (key_equal_size_.value >= Capacity) {\n        return detail::insert_code::full;\n      }\n      return detail::insert_code::inserted_at(\n          construct_at(index, std::forward<K>(key), std::forward<V>(value)));\n    }\n  }\n\n  template <typename K, typename V>\n  detail::insert_code::type insert_or_assign_result",
    "new": "      // Refuse a new key past Capacity even when a free slot is left.\n      return detail::insert_code::inserted_at(\n          construct_at(index, std::forward<K>(key), std::forward<V>(value)));\n    }\n  }\n\n  template <typename K, typename V>\n  detail::insert_code::type insert_or_assign_result",
}
