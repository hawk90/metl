"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::irq_lock on Cortex-M (#388)
MUTANT = {
    "name": "irq_lock_saves_after_disabling",
    "file": "include/metl/lock.hpp",
    "why": "PRIMASK is saved after interrupts are disabled, so it always reads "
           "masked and unlock never lets interrupts back in.",
    "kills": "qemu:cortex-m3:sync/lock_test.cpp",
    "old": """    __asm__ __volatile__("mrs %0, primask" : "=r"(previous)::"memory");
    __asm__ __volatile__("cpsid i" ::: "memory");""",
    "new": """    __asm__ __volatile__("cpsid i" ::: "memory");
    __asm__ __volatile__("mrs %0, primask" : "=r"(previous)::"memory");""",
}
