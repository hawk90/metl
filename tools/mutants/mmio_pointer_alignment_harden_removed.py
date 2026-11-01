"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "mmio_pointer_alignment_harden_removed",
    "file": "include/metl/mmio.hpp",
    "why": "a misaligned pointer reaches a volatile access unchecked, which hard-faults on Cortex-M0.",
    "kills": "ctest:harden_floor_memory",
    "old": "  METL_HARDEN(reinterpret_cast<std::uintptr_t>(p) % alignof(T) == 0);\n",
    "new": "  (void)p;\n",
}
