"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "unordered_set_find_iter_not_deprecated",
    "file": "include/metl/static_unordered_set.hpp",
    "why": "static_unordered_set::find_iter stops warning, so code that still uses it learns of "
           "the 2.0 removal only when it no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": 'METL_DEPRECATED("find_iter is removed in 2.0; use find_iterator (#645)")\n  METL_NODISCARD iterator find_iter(const key_type& key)',
    "new": 'METL_NODISCARD iterator find_iter(const key_type& key)',
}
