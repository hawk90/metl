"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "scheduler_snapshot_checks_address_only",
    "file": "include/metl/coro/scheduler.hpp",
    "why": "a task re-attached under another poll function mid-round gets the "
           "snapshot's old poll function -- a call through the wrong type when "
           "the storage holds a different object.",
    "kills": "ctest:coro_scheduler_reentrancy",
    "old": "    if (!holds(e.attachment)) {",
    "new": "    if (!is_attached(e.slot.task)) {",
}
