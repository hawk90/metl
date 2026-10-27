"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "isched_completion_reads_polled_task",
    "file": "tests/control/intrusive_scheduler.hpp",
    "why": "after a completing poll, the scheduler asks the polled task whether it is still linked instead of comparing the node before the cursor, so it reads a task the poll destroyed and removes an attachment the task made of itself.",
    "kills": "ctest:metl_intrusive_scheduler_test",
    "old": "    if (&*before == polled) {\n      tasks_.erase(before);\n    }",
    "new": "    (void)before;\n    auto* n = const_cast<task_node*>(polled);\n    if (n->is_linked()) {\n      tasks_.erase(*n);\n    }",
}
