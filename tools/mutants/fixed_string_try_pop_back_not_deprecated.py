"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_string_try_pop_back_not_deprecated",
    "file": "include/metl/fixed_string.hpp",
    "why": "fixed_string::try_pop_back stops warning, so code that still uses it learns of "
           "the 2.0 removal only when it no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": '  METL_DEPRECATED("try_pop_back is removed in 2.0; use if (!s.empty()) s.pop_back() (#645)")\n',
    "new": "",
}
