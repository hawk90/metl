"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "predefined_nodiscard_accepted",
    "file": "include/metl/attributes.hpp",
    "why": "The check for a predefined METL_NODISCARD tests a name nobody defines, "
           "so a build that defines METL_NODISCARD itself replaces METL's own "
           "definition without a diagnostic.",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": """#ifdef METL_NODISCARD
#error""",
    "new": """#ifdef METL_NODISCARD_NEVER_DEFINED
#error""",
}
