"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "unordered_set_emplace_capacity_harden_removed",
    "file": "include/metl/static_unordered_set.hpp",
    "why": "at METL_HARDENING_NONE, emplace inserts a new key past Capacity, so size() "
           "exceeds it and full() answers false (#579).",
    "kills": "ctest:harden_floor_memory",
    "old": '      METL_HARDEN(size_ < Capacity);\n      size_type index = npos;\n      const bool available = locate_insert_index(key, &index);\n      METL_ASSERT(available);\n      METL_ASSERT(state(index) != slot_state::occupied);\n      index = construct_at(index, std::forward<K>(key));',
    "new": '      size_type index = npos;\n      const bool available = locate_insert_index(key, &index);\n      METL_ASSERT(available);\n      METL_ASSERT(state(index) != slot_state::occupied);\n      index = construct_at(index, std::forward<K>(key));',
}
