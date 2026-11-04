"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "arena_allocate_not_deprecated",
    "file": "include/metl/arena_allocator.hpp",
    "why": "arena_allocator::allocate stops warning, so code that still uses it learns of "
           "the 2.0 change only when it asserts or no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": '  METL_DEPRECATED("allocate asserts on failure in 2.0; use try_allocate, which keeps returning null (#645)")\n',
    "new": '',
}
