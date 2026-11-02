"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "deadline_scheduler_null_poll_harden_removed",
    "file": "include/metl/coro/deadline_scheduler.hpp",
    "why": "a null poll is stored, and run_due jumps to address 0 (#570).",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(fn != nullptr);\n",
    "new": "",
}
