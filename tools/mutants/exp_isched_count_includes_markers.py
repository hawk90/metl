"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_isched_count_includes_markers",
    "file": "tests/exp/intrusive_scheduler.hpp",
    "why": "task_count() counts the running rounds' markers as tasks.",
    "kills": "ctest:metl_intrusive_scheduler_test",
    "old": "return tasks_.size() - (2 * rounds_);",
    "new": "return tasks_.size();",
}
