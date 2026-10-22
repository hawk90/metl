"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_isched_outer_marker_ends_round",
    "file": "tests/exp/intrusive_scheduler.hpp",
    "why": "a nested round stops at the outer round's cursor, taken for its own end, and polls no task after it.",
    "kills": "ctest:metl_intrusive_scheduler_test",
    "old": "      continue;  // an outer round's marker",
    "new": "      break;",
}
