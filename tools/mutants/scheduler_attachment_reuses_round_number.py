"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "scheduler_attachment_reuses_round_number",
    "file": "include/metl/coro/scheduler.hpp",
    "why": "A number taken mid-round matches a detached snapshot entry, so the "
           "round polls the detached task's stale slot (#310).",
    "kills": "ctest:coro_scheduler_reentrancy_test",
    "old": """    for (const round_frame* frame = round_; frame != nullptr; frame = frame->outer) {""",
    "new": """    for (const round_frame* frame = nullptr; frame != nullptr; frame = frame->outer) {""",
}
