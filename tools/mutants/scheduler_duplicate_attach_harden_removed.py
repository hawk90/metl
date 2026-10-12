"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "scheduler_duplicate_attach_harden_removed",
    "file": "include/metl/coro/scheduler.hpp",
    "why": "a task attached twice keeps an entry after detach() removes "
           "one, so the scheduler polls an object the caller destroyed.",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(!is_attached(task));",
    "new": "",
}
