"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_set_positional_at_not_deprecated",
    "file": "include/metl/flat_set.hpp",
    "why": "flat_set's const at stops warning, so code that still uses it learns of "
           "the 2.0 removal only when it no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": 'METL_DEPRECATED("positional at is removed in 2.0; use nth(index) (#645)")\n  METL_NODISCARD const_reference at(',
    "new": 'METL_NODISCARD const_reference at(',
}
