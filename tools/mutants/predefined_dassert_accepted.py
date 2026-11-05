"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "predefined_dassert_accepted",
    "file": "include/metl/config.hpp",
    "why": "The check for a predefined METL_DASSERT tests a name nobody defines, "
           "so a build that defines METL_DASSERT itself replaces METL's own "
           "definition without a diagnostic.",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": """#ifdef METL_DASSERT
#error""",
    "new": """#ifdef METL_DASSERT_NEVER_DEFINED
#error""",
}
