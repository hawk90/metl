"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "static_message_queue_not_deprecated",
    "file": "include/metl/static_message_queue.hpp",
    "why": "static_message_queue stops warning, so code that still uses it learns of "
           "the 2.0 removal only when it no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": 'class METL_DEPRECATED(\n    "static_message_queue is removed in 2.0; use guarded<fixed_queue<T, N>, irq_lock> (#166)")\n    static_message_queue',
    "new": 'class static_message_queue',
}
