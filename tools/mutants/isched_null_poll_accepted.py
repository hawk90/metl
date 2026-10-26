"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "isched_null_poll_accepted",
    "file": "tests/control/intrusive_scheduler.hpp",
    "why": "a null poll function is accepted, and the task reads as a round's marker that is never polled.",
    "kills": "ctest:metl_intrusive_scheduler_test",
    "old": "    METL_HARDEN(fn != nullptr);  // a null poll would read as a marker\n",
    "new": "",
}
