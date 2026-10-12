"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "deque_emplace_front_zero_capacity_harden_removed",
    "file": "include/metl/fixed_deque.hpp",
    "why": "emplace_front on a full zero-capacity deque returns front(), a "
           "reference to no object, at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": "      METL_HARDEN(this->size_ > 0);\n      return this->front();",
    "new": "      return this->front();",
}
