"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "scheduler_completion_removes_by_address",
    "file": "include/metl/coro/scheduler.hpp",
    "why": "completion removes whatever entry has the task's address, so a "
           "task that re-attached itself from the poll in which it completed "
           "loses the new attachment.",
    "kills": "ctest:coro_scheduler_reentrancy",
    "old": "      remove(e.attachment);",
    "new": "      (void)detach(e.slot.task);",
}
