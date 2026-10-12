"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "stepper_poll_reruns_finished_task",
    "file": "include/metl/coro/stepper.hpp",
    "why": "poll() keeps calling step() on a task that finished with done. "
           "It still returns false; only the extra step() calls show.",
    "kills": "ctest:coro_stepper_test",
    "old": "    if (done_ || error_) {",
    "new": "    if (error_) {",
}
