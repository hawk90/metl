"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::irq_lock on Cortex-M (#571)
MUTANT = {
    "name": "irq_lock_privilege_check_removed",
    "file": "include/metl/lock.hpp",
    "why": "irq_lock taken from unprivileged code masks nothing, and the debug "
           "build no longer says so.",
    "kills": "qemu:cortex-m3:sync/irq_lock_unprivileged_test.cpp",
    "old": "    METL_DASSERT(primask_set());\n",
    "new": "",
}
