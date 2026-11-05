"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "predefined_cacheline_size_accepted",
    "file": "include/metl/optimization.hpp",
    "why": "The check for a predefined METL_CACHELINE_SIZE tests a name nobody "
           "defines, so a build can change the layout of the three padded "
           "queues per translation unit again.",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": """#ifdef METL_CACHELINE_SIZE
#error""",
    "new": """#ifdef METL_CACHELINE_SIZE_NEVER_DEFINED
#error""",
}
