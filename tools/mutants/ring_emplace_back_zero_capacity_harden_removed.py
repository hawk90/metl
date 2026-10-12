"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "ring_emplace_back_zero_capacity_harden_removed",
    "file": "include/metl/detail/ring_core.hpp",
    "why": "emplace_back on a full zero-capacity ring returns back(), a "
           "reference to no object, at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": "      METL_HARDEN(size_ > 0);\n      return back();",
    "new": "      return back();",
}
