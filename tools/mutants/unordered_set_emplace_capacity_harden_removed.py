"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "unordered_set_emplace_capacity_harden_removed",
    "file": "include/metl/static_unordered_set.hpp",
    "why": "emplace and try_emplace insert a new key past Capacity while a free slot is left, so size() exceeds it and full() answers false (#579).",
    "kills": "ctest:harden_floor_memory",
    "old": "        return {iterator(this, index, detail::at_slot), false};\n      }\n      // Capacity is the element ceiling; bucket_count is the (larger) table size.\n      // Refuse a new key past Capacity even when a free slot is left.\n      if (key_equal_size_.value >= Capacity) {\n        return {end(), false};\n      }\n",
    "new": "        return {iterator(this, index, detail::at_slot), false};\n      }\n      // Capacity is the element ceiling; bucket_count is the (larger) table size.\n      // Refuse a new key past Capacity even when a free slot is left.\n",
}
