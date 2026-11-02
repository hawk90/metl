"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "scheduler_null_poll_harden_removed",
    "file": "include/metl/coro/scheduler.hpp",
    "why": "a null poll is stored as a live entry, and run_once jumps to address 0 (#570).",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(fn != nullptr);\n    size_type attachment = 0;",
    "new": "    size_type attachment = 0;",
}
