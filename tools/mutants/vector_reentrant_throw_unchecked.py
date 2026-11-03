"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::fixed_vector (#638)
MUTANT = {
    "name": "vector_reentrant_throw_unchecked",
    "file": "include/metl/fixed_vector.hpp",
    "why": "a constructor that pushes into its own vector and then throws is not "
           "refused: the slot whose constructor never finished stays counted as "
           "live, and the inner element sits outside size().",
    "kills": "ctest:reentrant_throw",
    "old": """      METL_HARDEN(size_ == index + 1);
""",
    "new": "",
}
