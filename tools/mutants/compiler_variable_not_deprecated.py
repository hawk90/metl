"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "compiler_variable_not_deprecated",
    "file": "include/metl/compiler.hpp",
    "why": "metl::is_clang stops warning, so code that still uses it learns of "
           "the 2.0 removal only when it no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": '[[deprecated("metl::is_clang is removed in 2.0; use METL_COMPILER_CLANG (#645)")]]\n',
    "new": '',
}
