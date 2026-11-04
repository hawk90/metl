"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "deadline_scheduler_task_count_not_deprecated",
    "file": "include/metl/coro/deadline_scheduler.hpp",
    "why": "deadline_scheduler::task_count stops warning, so code that still uses it learns of "
           "the 2.0 removal only when it no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": '  METL_DEPRECATED("task_count is removed in 2.0; use size() (#645)")\n',
    "new": "",
}
