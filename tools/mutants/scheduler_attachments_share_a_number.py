"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "scheduler_attachments_share_a_number",
    "file": "include/metl/coro/scheduler.hpp",
    "why": "every attachment gets the same number, so a mid-round attachment "
           "passes for the snapshot's entry and is polled in the round it was "
           "attached.",
    "kills": "ctest:coro_scheduler_reentrancy",
    "old": "entry{task_slot{task, fn}, attachment}",
    "new": "entry{task_slot{task, fn}, size_type{0}}",
}
