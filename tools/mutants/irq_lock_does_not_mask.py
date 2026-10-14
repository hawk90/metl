"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::irq_lock on Cortex-M (#388)
MUTANT = {
    "name": "irq_lock_does_not_mask",
    "file": "include/metl/lock.hpp",
    "why": "irq_lock saves PRIMASK but never sets it, so an interrupt still runs "
           "inside the critical section.",
    "kills": "qemu:cortex-m3:sync/irq_masking_test.cpp",
    "old": """    __asm__ __volatile__("mrs %0, primask" : "=r"(previous)::"memory");
    __asm__ __volatile__("cpsid i" ::: "memory");""",
    "new": """    __asm__ __volatile__("mrs %0, primask" : "=r"(previous)::"memory");""",
}
