"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::irq_lock on Cortex-M (#388)
MUTANT = {
    "name": "irq_lock_unlock_enables_unconditionally",
    "file": "include/metl/lock.hpp",
    "why": "unlock enables interrupts instead of restoring the saved PRIMASK, so "
           "releasing an inner lock lets interrupts into the outer critical section.",
    "kills": "qemu:cortex-m3:sync/irq_masking_test.cpp",
    "old": """    __asm__ __volatile__("msr primask, %0" ::"r"(previous) : "memory");""",
    "new": """    (void)previous;
    __asm__ __volatile__("cpsie i" ::: "memory");""",
}
