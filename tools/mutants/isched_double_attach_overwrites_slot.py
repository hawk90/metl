"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "isched_double_attach_overwrites_slot",
    "file": "tests/control/intrusive_scheduler.hpp",
    "why": "attaching an attached task writes its slot before the list traps, so the live attachment polls the second poll function.",
    "kills": "ctest:metl_intrusive_scheduler_test",
    "old": "    METL_HARDEN(!node.is_linked());\n",
    "new": "",
}
