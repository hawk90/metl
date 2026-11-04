"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "compiler_macro_not_deprecated",
    "file": "include/metl/compiler.hpp",
    "why": "METL_LIKELY stops warning, so code that still uses it learns of "
           "the 2.0 removal only when it no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": '#pragma clang deprecated(METL_LIKELY, "removed in 2.0; use METL_PREDICT_TRUE (#645)")\n',
    "new": '',
}
