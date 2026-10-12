"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "atomic_ref_alignment_harden_removed",
    "file": "include/metl/atomic_ref.hpp",
    "why": "an object aligned for T but not for std::atomic<T> is accessed "
           "by a misaligned atomic instruction.",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN((reinterpret_cast<std::uintptr_t>(ptr_) % required_alignment) == 0u);",
    "new": "",
}
