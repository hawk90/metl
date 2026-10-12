"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "spsc_capacity_assert_always_true",
    "file": "include/metl/spsc_queue.hpp",
    "why": "the power-of-two contract becomes unenforceable. Nothing in "
           "ctest notices, because no test constructs an invalid capacity "
           "-- it could not, it would not compile.",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": '  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");',
    "new": '  static_assert(true || ((Capacity & (Capacity - 1)) == 0), "Capacity must be power of two");',
}
