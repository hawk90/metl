"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "variant_not_deprecated",
    "file": "include/metl/variant.hpp",
    "why": "metl::variant stops warning, so code that still uses it learns of "
           "the 2.0 removal only when it no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": 'class METL_DEPRECATED("metl::variant is removed in 2.0; use std::variant (#635)") variant;',
    "new": 'class variant;',
}
