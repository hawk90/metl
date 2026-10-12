"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "scheduler_attachment_reuses_held_number",
    "file": "include/metl/coro/scheduler.hpp",
    "why": "A new attachment takes a number a live entry holds, so a completing "
           "attachment removes the other one (#310).",
    "kills": "ctest:coro_scheduler_reentrancy_test",
    "old": """    if (holds(attachment)) {
      return true;
    }
    for (const round_frame* frame""",
    "new": """    for (const round_frame* frame""",
}
