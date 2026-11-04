"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "static_allocator_not_deprecated",
    "file": "include/metl/static_allocator.hpp",
    "why": "static_allocator stops warning, so code that still uses it learns of "
           "the 2.0 change only when it asserts or no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": 'class METL_DEPRECATED("static_allocator is removed in 2.0; use monotonic_buffer and try_allocate_n<T> (#645)")\n    static_allocator {',
    "new": 'class static_allocator {',
}
