"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "predefined_assert_accepted",
    "file": "include/metl/config.hpp",
    "why": "The check for a predefined METL_ASSERT tests a name nobody defines, "
           "so a build that defines METL_ASSERT itself replaces METL's own "
           "definition without a diagnostic.",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": """#ifdef METL_ASSERT
#error""",
    "new": """#ifdef METL_ASSERT_NEVER_DEFINED
#error""",
}
