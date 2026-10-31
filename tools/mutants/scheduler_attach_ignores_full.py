"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "scheduler_attach_ignores_full",
    "file": "include/metl/coro/scheduler.hpp",
    "why": "attach on a full scheduler drops the task and carries on, so the task never runs and nothing says so.",
    "kills": "ctest:metl_utility_assert_trips_test",
    "old": '    METL_ASSERT(attached);',
    "new": '    (void)attached;',
}
