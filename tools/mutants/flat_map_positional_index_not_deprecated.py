"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_positional_index_not_deprecated",
    "file": "include/metl/flat_map.hpp",
    "why": "flat_map's const operator[] stops warning, so code that still uses it learns of "
           "the 2.0 removal only when it no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": 'METL_DEPRECATED("positional operator[] is removed in 2.0; use nth(index) (#645)")\n  METL_NODISCARD const_reference operator[]',
    "new": 'METL_NODISCARD const_reference operator[]',
}
