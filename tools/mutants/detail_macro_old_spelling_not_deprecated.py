"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "detail_macro_old_spelling_not_deprecated",
    "file": "include/metl/detail/construct.hpp",
    "why": "METL_CONSTEXPR20, the old spelling of an internal helper, stops "
           "warning, so code that still uses it learns of the 2.0 removal only "
           "when it no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": '#pragma clang deprecated(METL_CONSTEXPR20, \\\n',
    "new": "",
}
