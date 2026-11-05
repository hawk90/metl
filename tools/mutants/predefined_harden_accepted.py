"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "predefined_harden_accepted",
    "file": "include/metl/config.hpp",
    "why": "The check for a predefined METL_HARDEN tests a name nobody defines, "
           "so a build that defines METL_HARDEN itself replaces METL's own "
           "definition without a diagnostic.",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": """#ifdef METL_HARDEN
#error""",
    "new": """#ifdef METL_HARDEN_NEVER_DEFINED
#error""",
}
